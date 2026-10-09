#include "rtp_receiver.h"
#include "config.h"
#include "gstreamer_pipeline.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

// ============================================================
// RTP BYTE ORDER HELPERS
// ============================================================

uint16_t readBE16(const unsigned char* p)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8) |
        p[1]
    );
}


uint32_t readBE32(const unsigned char* p)
{
    return
        (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8) |
        static_cast<uint32_t>(p[3]);
}


// ============================================================
// MPEG-TS VALIDATION
// ============================================================
//
// RTP payload type 33 means MPEG-TS.
//
// MPEG-TS packets are 188 bytes and each packet starts with
// the sync byte:
//
//     0x47
//
// We use this only to determine when actual media data has
// arrived. GStreamer is NOT started before this point.
// ============================================================

bool looksLikeMpegTs(
    const unsigned char* data,
    size_t size)
{
    if (size < 188)
        return false;


    /*
     * First MPEG-TS packet.
     */
    if (data[0] != 0x47)
        return false;


    /*
     * If multiple complete TS packets are present,
     * verify their sync bytes as well.
     */
    if (size >= 376 &&
        data[188] != 0x47)
    {
        return false;
    }


    if (size >= 564 &&
        data[376] != 0x47)
    {
        return false;
    }


    return true;
}


// ============================================================
// THREAD-SAFE BOUNDED PACKET QUEUE
// ============================================================

class PacketQueue
{
public:

    /*
     * Maximum amount of MPEG-TS data allowed to wait
     * between RTP reception and GStreamer.
     */
    static constexpr size_t MAX_BYTES =
        4 * 1024 * 1024;


    void push(
        const unsigned char* data,
        size_t size)
    {
        if (size == 0)
            return;


        std::vector<uint8_t> packet(
            data,
            data + size
        );


        std::unique_lock<std::mutex> lock(mutex_);


        if (stopped_)
            return;


        /*
         * Keep queue bounded.
         *
         * If GStreamer is slower than the network,
         * discard OLD packets instead of allowing
         * latency to grow indefinitely.
         */
        while (
            currentBytes_ + size > MAX_BYTES &&
            !queue_.empty())
        {
            currentBytes_ -= queue_.front().size();

            queue_.pop_front();

            ++droppedPackets_;
        }


        /*
         * If one individual packet is larger than
         * the entire queue limit, don't store it.
         */
        if (size > MAX_BYTES)
        {
            ++droppedPackets_;

            return;
        }


        queue_.push_back(
            std::move(packet)
        );


        currentBytes_ += size;


        lock.unlock();

        condition_.notify_one();
    }


    bool pop(
        std::vector<uint8_t>& packet)
    {
        std::unique_lock<std::mutex> lock(
            mutex_
        );


        condition_.wait(
            lock,
            [this]()
            {
                return
                    stopped_ ||
                    !queue_.empty();
            }
        );


        if (queue_.empty())
        {
            return false;
        }


        packet =
            std::move(queue_.front());


        queue_.pop_front();


        currentBytes_ -=
            packet.size();


        return true;
    }


    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex_
            );

            stopped_ = true;
        }


        condition_.notify_all();
    }


    size_t size()
    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        return queue_.size();
    }


    size_t bytes()
    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        return currentBytes_;
    }


    unsigned long long droppedPackets()
    {
        std::lock_guard<std::mutex> lock(
            mutex_
        );

        return droppedPackets_;
    }


private:

    std::deque<std::vector<uint8_t>> queue_;

    std::mutex mutex_;

    std::condition_variable condition_;

    size_t currentBytes_ = 0;

    unsigned long long droppedPackets_ = 0;

    bool stopped_ = false;
};

} // namespace


// ============================================================
// RTP RECEIVER
// ============================================================

void rtpReceiver()
{
    // --------------------------------------------------------
    // SOCKET
    // --------------------------------------------------------

    int sock = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );


    if (sock < 0)
    {
        std::cerr
            << "[RTP] socket() failed: "
            << std::strerror(errno)
            << '\n';

        return;
    }


    // --------------------------------------------------------
    // SOCKET REUSE
    // --------------------------------------------------------

    int reuse = 1;


    setsockopt(
        sock,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );


    // --------------------------------------------------------
    // UDP RECEIVE BUFFER
    // --------------------------------------------------------

    int receiveBufferSize =
        4 * 1024 * 1024;


    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_RCVBUF,
            &receiveBufferSize,
            sizeof(receiveBufferSize)) < 0)
    {
        std::cerr
            << "[RTP] Warning: failed to increase "
               "UDP receive buffer: "
            << std::strerror(errno)
            << '\n';
    }


    // --------------------------------------------------------
    // RECEIVE TIMEOUT
    // --------------------------------------------------------

    timeval timeout{};

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;


    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) < 0)
    {
        std::cerr
            << "[RTP] Warning: failed to set "
               "receive timeout: "
            << std::strerror(errno)
            << '\n';
    }


    // --------------------------------------------------------
    // BIND
    // --------------------------------------------------------

    sockaddr_in addr{};

    addr.sin_family = AF_INET;

    addr.sin_port =
        htons(Config::RTP_PORT);

    addr.sin_addr.s_addr =
        htonl(INADDR_ANY);


    if (bind(
            sock,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)) < 0)
    {
        std::cerr
            << "[RTP] bind() failed: "
            << std::strerror(errno)
            << '\n';

        close(sock);

        return;
    }


    std::cout
        << '\n'
        << "==========================================\n"
        << "[RTP] Listening on UDP "
        << Config::RTP_PORT
        << '\n'
        << "==========================================\n\n";


    // ========================================================
    // GSTREAMER
    // ========================================================
    //
    // IMPORTANT:
    //
    // GStreamer is CREATED here, but NOT STARTED here.
    //
    // start() will only be called after the first valid
    // MPEG-TS RTP packet has arrived.
    // ========================================================

    GStreamerPipeline gstPipeline;


    std::atomic<bool> gstStarted{false};


    // ========================================================
    // SHARED STATE
    // ========================================================

    std::atomic<bool> running{true};


    PacketQueue packetQueue;


    // ========================================================
    // GSTREAMER CONSUMER THREAD
    // ========================================================
    //
    // This thread is the ONLY thread that calls:
    //
    //     gstPipeline.push()
    //
    // The RTP receive thread only places media packets
    // into the queue.
    //
    // Before the first valid media packet:
    //
    //     queue = empty
    //     gstStarted = false
    //
    // Therefore this thread simply waits.
    // ========================================================

    std::thread gstThread(
        [&]()
        {
            std::cout
                << "[GST-THREAD] Started\n";


            std::vector<uint8_t> payload;


            while (running)
            {
                if (!packetQueue.pop(payload))
                {
                    break;
                }


                if (payload.empty())
                {
                    continue;
                }


                /*
                 * This should only happen after the RTP thread
                 * has successfully started GStreamer.
                 */
                if (!gstStarted.load())
                {
                    std::cerr
                        << "[GST-THREAD] ERROR: received "
                           "payload before GStreamer started\n";

                    continue;
                }


                if (!gstPipeline.push(
                        payload.data(),
                        payload.size()))
                {
                    std::cerr
                        << "[GST-THREAD] "
                           "Failed to push payload\n";

                    running = false;

                    packetQueue.stop();

                    break;
                }
            }


            std::cout
                << "[GST-THREAD] Stopped\n";
        }
    );


    std::cout
        << "[RTP] GStreamer forwarding thread ready\n";

    std::cout
        << "[RTP] Waiting for first valid MPEG-TS media...\n";


    // ========================================================
    // STATISTICS
    // ========================================================

    unsigned long long packetCount = 0;

    unsigned long long byteCount = 0;

    unsigned long long invalidRtpCount = 0;

    unsigned long long sequenceGapCount = 0;


    // RTP sequence tracking.

    bool haveSequence = false;

    uint16_t expectedSequence = 0;


    // Maximum UDP packet.

    unsigned char packet[65535];


    // ========================================================
    // RTP RECEIVE LOOP
    // ========================================================

    while (running)
    {
        sockaddr_in source{};

        socklen_t sourceLen =
            sizeof(source);


        ssize_t length = recvfrom(
            sock,
            packet,
            sizeof(packet),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &sourceLen
        );


        // ----------------------------------------------------
        // RECEIVE ERROR
        // ----------------------------------------------------

        if (length < 0)
        {
            if (errno == EINTR)
                continue;


            /*
             * Timeout is normal.
             */
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)
            {
                continue;
            }


            std::cerr
                << "[RTP] recvfrom(): "
                << std::strerror(errno)
                << '\n';


            running = false;

            break;
        }


        // ----------------------------------------------------
        // MINIMUM RTP HEADER
        // ----------------------------------------------------

        if (length < 12)
        {
            ++invalidRtpCount;


            std::cerr
                << "[RTP] Packet too small: "
                << length
                << '\n';


            continue;
        }


        // ----------------------------------------------------
        // RTP FIXED HEADER
        // ----------------------------------------------------

        uint8_t version =
            (packet[0] >> 6) & 0x03;


        bool padding =
            (packet[0] & 0x20) != 0;


        bool extension =
            (packet[0] & 0x10) != 0;


        uint8_t csrcCount =
            packet[0] & 0x0F;


        uint8_t payloadType =
            packet[1] & 0x7F;


        uint16_t sequence =
            readBE16(packet + 2);


        uint32_t timestamp =
            readBE32(packet + 4);


        uint32_t ssrc =
            readBE32(packet + 8);


        // ----------------------------------------------------
        // RTP VERSION
        // ----------------------------------------------------

        if (version != 2)
        {
            ++invalidRtpCount;


            std::cerr
                << "[RTP] Invalid RTP version: "
                << static_cast<int>(version)
                << '\n';


            continue;
        }


        // ----------------------------------------------------
        // RTP HEADER LENGTH
        // ----------------------------------------------------

        size_t headerLen =
            12 +
            static_cast<size_t>(
                csrcCount
            ) * 4;


        if (
            static_cast<size_t>(length) <
            headerLen
        )
        {
            ++invalidRtpCount;


            std::cerr
                << "[RTP] Invalid CSRC/header length\n";


            continue;
        }


        // ----------------------------------------------------
        // RTP EXTENSION
        // ----------------------------------------------------

        if (extension)
        {
            if (
                static_cast<size_t>(length) <
                headerLen + 4
            )
            {
                ++invalidRtpCount;


                std::cerr
                    << "[RTP] Invalid RTP extension\n";


                continue;
            }


            uint16_t extensionLength =
                readBE16(
                    packet +
                    headerLen +
                    2
                );


            size_t extensionBytes =
                static_cast<size_t>(
                    extensionLength
                ) * 4;


            if (
                static_cast<size_t>(length) <
                headerLen +
                4 +
                extensionBytes
            )
            {
                ++invalidRtpCount;


                std::cerr
                    << "[RTP] Invalid RTP extension length\n";


                continue;
            }


            headerLen +=
                4 +
                extensionBytes;
        }


        // ----------------------------------------------------
        // RTP PAYLOAD
        // ----------------------------------------------------

        if (
            static_cast<size_t>(length) <=
            headerLen
        )
        {
            ++invalidRtpCount;

            continue;
        }


        size_t payloadLen =
            static_cast<size_t>(length) -
            headerLen;


        unsigned char* payload =
            packet + headerLen;


        // ----------------------------------------------------
        // RTP PADDING
        // ----------------------------------------------------

        if (padding)
        {
            uint8_t paddingLen =
                packet[length - 1];


            if (
                paddingLen == 0 ||
                paddingLen > payloadLen
            )
            {
                ++invalidRtpCount;


                std::cerr
                    << "[RTP] Invalid RTP padding\n";


                continue;
            }


            payloadLen -= paddingLen;
        }


        // ----------------------------------------------------
        // PAYLOAD TYPE
        // ----------------------------------------------------
        //
        // 33 = MPEG-TS
        // ----------------------------------------------------

        if (payloadType != 33)
        {
            std::cerr
                << "[RTP] Ignoring payload type "
                << static_cast<int>(payloadType)
                << '\n';


            continue;
        }


        // ----------------------------------------------------
        // SEQUENCE MONITORING
        // ----------------------------------------------------

        if (haveSequence)
        {
            uint16_t expected =
                expectedSequence;


            if (sequence != expected)
            {
                uint16_t missing =
                    static_cast<uint16_t>(
                        sequence - expected
                    );


                if (
                    missing != 0 &&
                    missing < 0x8000
                )
                {
                    ++sequenceGapCount;


                    std::cerr
                        << "[RTP] Sequence gap: "
                        << "expected="
                        << expected
                        << " received="
                        << sequence
                        << " missing~="
                        << missing
                        << '\n';
                }
            }
        }


        expectedSequence =
            static_cast<uint16_t>(
                sequence + 1
            );


        haveSequence = true;


        // ====================================================
        // FIRST VALID MPEG-TS MEDIA
        // ====================================================
        //
        // This is the important lifecycle change.
        //
        // We do NOT start GStreamer when the RTP receiver
        // starts.
        //
        // We do NOT start GStreamer when RTSP connects.
        //
        // We do NOT start GStreamer when PLAY is accepted.
        //
        // We start it only when actual MPEG-TS media data
        // has arrived.
        // ====================================================

        if (!gstStarted.load())
        {
            if (!looksLikeMpegTs(
                    payload,
                    payloadLen))
            {
                /*
                 * Payload type says MPEG-TS, but the actual
                 * payload doesn't look like MPEG-TS yet.
                 *
                 * Don't start GStreamer.
                 */
                continue;
            }


            std::cout
                << '\n'
                << "[RTP] First valid MPEG-TS media received.\n"
                << "[RTP] Starting GStreamer...\n";


            if (!gstPipeline.start())
            {
                std::cerr
                    << "[RTP] Failed to start GStreamer pipeline\n";


                running = false;

                packetQueue.stop();

                break;
            }


            gstStarted.store(true);


            std::cout
                << "[RTP] GStreamer started!\n";


            std::cout
                << "[RTP] Forwarding media to GStreamer...\n\n";
        }


        // ====================================================
        // FIRST PACKET DEBUG
        // ====================================================

        if (packetCount == 0)
        {
            char sourceIp[
                INET_ADDRSTRLEN
            ]{};


            inet_ntop(
                AF_INET,
                &source.sin_addr,
                sourceIp,
                sizeof(sourceIp)
            );


            std::cout
                << "[RTP] First packet:\n"
                << "       source     = "
                << sourceIp
                << ':'
                << ntohs(source.sin_port)
                << '\n'
                << "       sequence   = "
                << sequence
                << '\n'
                << "       timestamp  = "
                << timestamp
                << '\n'
                << "       SSRC       = "
                << ssrc
                << '\n'
                << "       RTP size   = "
                << length
                << '\n'
                << "       RTP header = "
                << headerLen
                << '\n'
                << "       payload    = "
                << payloadLen
                << " bytes\n"
                << "       payload[0:16] = ";


            size_t printLen =
                payloadLen < 16
                    ? payloadLen
                    : 16;


            for (
                size_t i = 0;
                i < printLen;
                ++i
            )
            {
                std::printf(
                    "%02x%s",
                    payload[i],
                    (i + 1 == printLen)
                        ? ""
                        : " "
                );
            }


            std::printf("\n\n");
        }


        // ====================================================
        // QUEUE MPEG-TS
        // ====================================================
        //
        // GStreamer has already been started above.
        //
        // The GST consumer thread is the only thread that
        // actually calls gstPipeline.push().
        // ====================================================

        packetQueue.push(
            payload,
            payloadLen
        );


        // ----------------------------------------------------
        // STATISTICS
        // ----------------------------------------------------

        ++packetCount;

        byteCount += payloadLen;


        if (packetCount % 100 == 0)
        {
            std::cout
                << "[RTP] packets="
                << packetCount
                << " payload_bytes="
                << byteCount
                << " seq="
                << sequence
                << " timestamp="
                << timestamp
                << " invalid_rtp="
                << invalidRtpCount
                << " seq_gaps="
                << sequenceGapCount
                << " queue_packets="
                << packetQueue.size()
                << " queue_bytes="
                << packetQueue.bytes()
                << " dropped="
                << packetQueue.droppedPackets()
                << '\n';
        }
    }


    // ========================================================
    // STOP RECEIVING
    // ========================================================

    running = false;


    // Wake GStreamer thread.

    packetQueue.stop();


    // ========================================================
    // WAIT FOR GSTREAMER THREAD
    // ========================================================

    if (gstThread.joinable())
    {
        gstThread.join();
    }


    // ========================================================
    // STOP GSTREAMER
    // ========================================================
    //
    // stop() should be safe even if GStreamer was never
    // started, because the receiver may terminate before
    // receiving any media.
    // ========================================================

    if (gstStarted.load())
    {
        gstPipeline.stop();
    }


    // ========================================================
    // CLOSE SOCKET
    // ========================================================

    close(sock);


    // ========================================================
    // FINAL STATISTICS
    // ========================================================

    std::cout
        << "\n[RTP] Receiver stopped.\n"
        << "[RTP] Packets: "
        << packetCount
        << '\n'
        << "[RTP] Payload bytes: "
        << byteCount
        << '\n'
        << "[RTP] Invalid RTP: "
        << invalidRtpCount
        << '\n'
        << "[RTP] Sequence gaps: "
        << sequenceGapCount
        << '\n'
        << "[RTP] Queue dropped packets: "
        << packetQueue.droppedPackets()
        << '\n';
}
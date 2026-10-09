#include "config.h"
#include "rtcp_receiver.h"
#include "rtp_receiver.h"
#include "rtsp_parser.h"
#include "rtsp_utils.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr int CONTROL_PORT = 9999;


int connectToPhone()
{
    int sock = createTcpSocket();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Config::RTSP_PORT);

    if (inet_pton(
        AF_INET,
        Config::PHONE_IP,
        &addr.sin_addr
    ) != 1) {

        close(sock);
        throw std::runtime_error("Invalid PHONE_IP");
    }

    std::cout
        << "[RTSP] Connecting to "
        << Config::PHONE_IP
        << ':'
        << Config::RTSP_PORT
        << '\n';

    if (connect(
        sock,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr)
    ) < 0) {

        std::string error =
            std::string("connect(): ") +
            std::strerror(errno);

        close(sock);
        throw std::runtime_error(error);
    }

    std::cout << "[RTSP] Connected!\n";

    return sock;
}


/*
 * Create localhost control socket.
 *
 * Python controller connects to:
 *
 *     127.0.0.1:9999
 *
 * Commands:
 *
 *     PAUSE
 *     PLAY
 *     QUIT
 */
int createControlSocket()
{
    int sock = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (sock < 0) {
        throw std::runtime_error(
            std::string("[CONTROL] socket(): ") +
            std::strerror(errno)
        );
    }

    int reuse = 1;

    setsockopt(
        sock,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(CONTROL_PORT);

    if (bind(
        sock,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr)
    ) < 0) {

        std::string error =
            std::string("[CONTROL] bind(): ") +
            std::strerror(errno);

        close(sock);

        throw std::runtime_error(error);
    }

    if (listen(sock, 5) < 0) {

        std::string error =
            std::string("[CONTROL] listen(): ") +
            std::strerror(errno);

        close(sock);

        throw std::runtime_error(error);
    }

    std::cout
        << "[CONTROL] Listening on "
        << "127.0.0.1:"
        << CONTROL_PORT
        << '\n';

    return sock;
}


/*
 * Send RTSP PLAY through the EXISTING RTSP connection.
 */
void sendPlay(
    int rtspSock,
    const std::string& sessionId,
    int& nextCseq
)
{
    if (sessionId.empty()) {
        std::cout
            << "[CONTROL] No RTSP session yet.\n";
        return;
    }

    int cseq = nextCseq++;

    std::string play =
        "PLAY " +
        Config::wfdUri() +
        " RTSP/1.0\r\n"
        "CSeq: " +
        std::to_string(cseq) +
        "\r\n"
        "Session: " +
        sessionId +
        "\r\n\r\n";

    std::cout
        << "\n[CONTROL] → PLAY\n"
        << "[CONTROL] CSeq="
        << cseq
        << '\n'
        << "[CONTROL] Session="
        << sessionId
        << '\n';

    sendAll(rtspSock, play);
}


/*
 * Send RTSP PAUSE through the EXISTING RTSP connection.
 */
void sendPause(
    int rtspSock,
    const std::string& sessionId,
    int& nextCseq
)
{
    if (sessionId.empty()) {
        std::cout
            << "[CONTROL] No RTSP session yet.\n";
        return;
    }

    int cseq = nextCseq++;

    std::string pause =
        "PAUSE " +
        Config::wfdUri() +
        " RTSP/1.0\r\n"
        "CSeq: " +
        std::to_string(cseq) +
        "\r\n"
        "Session: " +
        sessionId +
        "\r\n\r\n";

    std::cout
        << "\n[CONTROL] → PAUSE\n"
        << "[CONTROL] CSeq="
        << cseq
        << '\n'
        << "[CONTROL] Session="
        << sessionId
        << '\n';

    sendAll(rtspSock, pause);
}


/*
 * Handle one command from Python.
 */
bool handleControlCommand(
    int clientSock,
    int rtspSock,
    const std::string& sessionId,
    int& nextCseq
)
{
    char buffer[256]{};

    ssize_t n = recv(
        clientSock,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (n <= 0)
        return false;

    std::string command(
        buffer,
        static_cast<size_t>(n)
    );

    /*
     * Remove whitespace/newlines.
     */
    while (!command.empty() &&
           std::isspace(
               static_cast<unsigned char>(command.back())
           )) {

        command.pop_back();
    }

    while (!command.empty() &&
           std::isspace(
               static_cast<unsigned char>(command.front())
           )) {

        command.erase(command.begin());
    }

    /*
     * Convert to uppercase.
     */
    for (char& c : command) {

        c = static_cast<char>(
            std::toupper(
                static_cast<unsigned char>(c)
            )
        );
    }

    std::cout
        << "[CONTROL] Received: "
        << command
        << '\n';


    if (command == "PAUSE") {

        sendPause(
            rtspSock,
            sessionId,
            nextCseq
        );

        const char* response =
            "PAUSE SENT\n";

        send(
            clientSock,
            response,
            std::strlen(response),
            0
        );
    }


    else if (command == "PLAY") {

        sendPlay(
            rtspSock,
            sessionId,
            nextCseq
        );

        const char* response =
            "PLAY SENT\n";

        send(
            clientSock,
            response,
            std::strlen(response),
            0
        );
    }


    else if (command == "QUIT") {

        const char* response =
            "QUIT\n";

        send(
            clientSock,
            response,
            std::strlen(response),
            0
        );

        return true;
    }


    else {

        const char* response =
            "UNKNOWN COMMAND\n";

        send(
            clientSock,
            response,
            std::strlen(response),
            0
        );
    }

    return false;
}

} // namespace


int main()
{
    try {

        /*
         * =====================================================
         * Start RTP receiver
         * =====================================================
         *
         * IMPORTANT:
         *
         * RTP receiver starts listening immediately.
         *
         * GStreamer does NOT start here.
         *
         * GStreamer will be started by the RTP receiver
         * only after the first valid MPEG-TS media packet
         * arrives.
         */
        std::thread rtpThread(rtpReceiver);
        rtpThread.detach();


        /*
         * =====================================================
         * Start RTCP receiver
         * =====================================================
         */
        std::thread rtcpThread(rtcpReceiver);
        rtcpThread.detach();


        /*
         * Give RTP/RTCP sockets a moment to initialize.
         */
        usleep(200000);


        std::cout
            << '\n'
            << "==========================================\n"
            << "Connecting to Android RTSP server\n"
            << "==========================================\n\n";


        /*
         * =====================================================
         * Connect to Android RTSP server
         * =====================================================
         */
        int sock = connectToPhone();


        /*
         * =====================================================
         * Local control socket
         * =====================================================
         */
        int controlSock = createControlSocket();


        RTSPParser parser;

        int nextCseq = 1;

        int setupCseq = -1;
        int playCseq = -1;

        std::string sessionId;


        /*
         * =====================================================
         * Initial OPTIONS
         * =====================================================
         */
        int optionsCseq = nextCseq++;

        std::string options =
            "OPTIONS * RTSP/1.0\r\n"
            "CSeq: " +
            std::to_string(optionsCseq) +
            "\r\n\r\n";

        std::cout
            << "\n[RTSP] → OPTIONS\n"
            << "[RTSP] CSeq="
            << optionsCseq
            << '\n';

        sendAll(sock, options);


        /*
         * =====================================================
         * Main event loop
         * =====================================================
         *
         * sock:
         *     Android RTSP connection
         *
         * controlSock:
         *     Python control connection
         */
        char buffer[65536];

        bool quit = false;

        while (!quit) {

            struct pollfd fds[2]{};

            fds[0].fd = sock;
            fds[0].events = POLLIN;

            fds[1].fd = controlSock;
            fds[1].events = POLLIN;


            int result = poll(
                fds,
                2,
                -1
            );


            if (result < 0) {

                if (errno == EINTR)
                    continue;

                throw std::runtime_error(
                    std::string("poll(): ") +
                    std::strerror(errno)
                );
            }


            /*
             * =================================================
             * Python control socket
             * =================================================
             */
            if (fds[1].revents & POLLIN) {

                int clientSock = accept(
                    controlSock,
                    nullptr,
                    nullptr
                );

                if (clientSock >= 0) {

                    quit =
                        handleControlCommand(
                            clientSock,
                            sock,
                            sessionId,
                            nextCseq
                        );

                    close(clientSock);
                }
            }


            /*
             * =================================================
             * Android RTSP socket
             * =================================================
             */
            if (fds[0].revents & POLLIN) {

                ssize_t n = recv(
                    sock,
                    buffer,
                    sizeof(buffer),
                    0
                );


                if (n < 0) {

                    if (errno == EINTR)
                        continue;

                    throw std::runtime_error(
                        std::string("recv(): ") +
                        std::strerror(errno)
                    );
                }


                if (n == 0) {

                    std::cout
                        << "[RTSP] Connection closed.\n";

                    break;
                }


                auto messages =
                    parser.feed(
                        buffer,
                        static_cast<size_t>(n)
                    );


                for (const auto& message : messages) {

                    if (message.lines.empty())
                        continue;


                    const std::string& firstLine =
                        message.lines[0];


                    int cseq =
                        getCSeq(message.lines);


                    /*
                     * =========================================
                     * RTSP RESPONSE
                     * =========================================
                     */
                    if (
                        firstLine.rfind(
                            "RTSP/",
                            0
                        ) == 0
                    ) {

                        std::cout
                            << "\n[RTSP] ← "
                            << firstLine
                            << " CSeq="
                            << cseq
                            << '\n';


                        std::string session =
                            getHeader(
                                message.lines,
                                "Session"
                            );


                        std::string transport =
                            getHeader(
                                message.lines,
                                "Transport"
                            );


                        if (!session.empty()) {

                            std::cout
                                << "[RTSP] Session: "
                                << session
                                << '\n';
                        }


                        if (!transport.empty())
                            parseTransport(transport);


                        /*
                         * =====================================
                         * SETUP response
                         * =====================================
                         */
                        if (
                            setupCseq != -1 &&
                            cseq == setupCseq
                        ) {

                            std::cout
                                << "\n[WFD] SETUP accepted.\n";


                            size_t semicolon =
                                session.find(';');


                            if (
                                semicolon ==
                                std::string::npos
                            ) {

                                sessionId =
                                    session;

                            } else {

                                sessionId =
                                    session.substr(
                                        0,
                                        semicolon
                                    );
                            }


                            if (sessionId.empty()) {

                                std::cerr
                                    << "[RTSP] ERROR: "
                                       "SETUP response "
                                       "contained no Session.\n";

                                continue;
                            }


                            /*
                             * =================================
                             * Initial PLAY
                             * =================================
                             */
                            playCseq =
                                nextCseq++;


                            std::string play =
                                "PLAY " +
                                Config::wfdUri() +
                                " RTSP/1.0\r\n"
                                "CSeq: " +
                                std::to_string(playCseq) +
                                "\r\n"
                                "Session: " +
                                sessionId +
                                "\r\n\r\n";


                            std::cout
                                << "\n[RTSP] → PLAY\n"
                                << "[RTSP] CSeq="
                                << playCseq
                                << '\n'
                                << "[RTSP] Session="
                                << sessionId
                                << '\n';


                            sendAll(
                                sock,
                                play
                            );
                        }


                        /*
                         * =====================================
                         * Initial PLAY response
                         * =====================================
                         */
                        else if (
                            playCseq != -1 &&
                            cseq == playCseq
                        ) {

                            std::cout
                                << "\n"
                                << "==========================================\n"
                                << "[RTSP] PLAY accepted!\n"
                                << "[RTP] Waiting for first valid media packet...\n"
                                << "[GST] GStreamer will start when media arrives.\n"
                                << "==========================================\n\n";
                        }


                        continue;
                    }


                    /*
                     * =========================================
                     * RTSP REQUEST FROM ANDROID
                     * =========================================
                     */
                    std::cout
                        << "\n[RTSP] ← REQUEST: "
                        << firstLine
                        << '\n'
                        << "[RTSP] CSeq: "
                        << cseq
                        << '\n';


                    std::string body =
                        message.body;


                    if (!body.empty()) {

                        std::cout
                            << "[RTSP] Body:\n"
                            << body
                            << '\n';
                    }


                    /*
                     * =================================================
                     * OPTIONS
                     * =================================================
                     */
                    if (
                        firstLine.rfind(
                            "OPTIONS",
                            0
                        ) == 0
                    ) {

                        std::string response =
                            "RTSP/1.0 200 OK\r\n"
                            "CSeq: " +
                            std::to_string(cseq) +
                            "\r\n"
                            "Public: org.wfa.wfd1.0, "
                            "SETUP, TEARDOWN, PLAY, PAUSE, "
                            "GET_PARAMETER, SET_PARAMETER\r\n"
                            "\r\n";


                        std::cout
                            << "[RTSP] → OPTIONS 200 OK\n";


                        sendAll(
                            sock,
                            response
                        );
                    }


                    /*
                     * =================================================
                     * GET_PARAMETER
                     * =================================================
                     */
                    else if (
                        firstLine.rfind(
                            "GET_PARAMETER",
                            0
                        ) == 0
                    ) {

                        std::string lower =
                            body;


                        for (char& c : lower) {

                            c = static_cast<char>(
                                std::tolower(
                                    static_cast<unsigned char>(c)
                                )
                            );
                        }


                        std::string responseBody;


                        if (
                            lower.find(
                                "wfd_video_formats"
                            ) != std::string::npos
                        ) {

                            responseBody +=
                                "wfd_video_formats: " +
                                std::string(
                                    Config::WFD_VIDEO_FORMATS
                                ) +
                                "\r\n";
                        }


                        if (
                            lower.find(
                                "wfd_audio_codecs"
                            ) != std::string::npos
                        ) {

                            responseBody +=
                                "wfd_audio_codecs: " +
                                std::string(
                                    Config::WFD_AUDIO_CODECS
                                ) +
                                "\r\n";
                        }


                        if (
                            lower.find(
                                "wfd_client_rtp_ports"
                            ) != std::string::npos
                        ) {

                            responseBody +=
                                "wfd_client_rtp_ports: " +
                                Config::wfdRtpPorts() +
                                "\r\n";
                        }


                        std::cout
                            << "\n"
                            << "[RTSP] → "
                               "GET_PARAMETER response:\n"
                            << responseBody
                            << '\n';


                        sendRtspResponse(
                            sock,
                            cseq,
                            responseBody
                        );
                    }


                    /*
                     * =================================================
                     * SET_PARAMETER
                     * =================================================
                     */
                    else if (
                        firstLine.rfind(
                            "SET_PARAMETER",
                            0
                        ) == 0
                    ) {

                        std::string lower =
                            body;


                        for (char& c : lower) {

                            c = static_cast<char>(
                                std::tolower(
                                    static_cast<unsigned char>(c)
                                )
                            );
                        }


                        bool requestsSetup =
                            lower.find(
                                "wfd_trigger_method:"
                            ) != std::string::npos &&
                            lower.find(
                                "setup"
                            ) != std::string::npos;


                        if (requestsSetup) {

                            std::cout
                                << "\n"
                                << "[WFD] Android "
                                   "requested SETUP.\n";


                            sendRtspResponse(
                                sock,
                                cseq
                            );


                            std::cout
                                << "[RTSP] → "
                                   "SET_PARAMETER 200 OK\n";


                            setupCseq =
                                nextCseq++;


                            std::string setup =
                                "SETUP " +
                                Config::wfdUri() +
                                " RTSP/1.0\r\n"
                                "CSeq: " +
                                std::to_string(setupCseq) +
                                "\r\n"
                                "Transport: RTP/AVP/UDP;"
                                "unicast;"
                                "client_port=" +
                                std::to_string(
                                    Config::RTP_PORT
                                ) +
                                "-" +
                                std::to_string(
                                    Config::RTCP_PORT
                                ) +
                                "\r\n\r\n";


                            std::cout
                                << "\n"
                                << "[RTSP] → SETUP\n"
                                << "[RTSP] CSeq="
                                << setupCseq
                                << '\n'
                                << "[RTSP] Client ports="
                                << Config::RTP_PORT
                                << '-'
                                << Config::RTCP_PORT
                                << '\n';


                            sendAll(
                                sock,
                                setup
                            );

                        } else {

                            sendRtspResponse(
                                sock,
                                cseq
                            );
                        }
                    }


                    /*
                     * =================================================
                     * PLAY from Android
                     * =================================================
                     */
                    else if (
                        firstLine.rfind(
                            "PLAY",
                            0
                        ) == 0
                    ) {

                        std::cout
                            << "[RTSP] Android sent PLAY.\n";


                        sendRtspResponse(
                            sock,
                            cseq
                        );
                    }


                    /*
                     * =================================================
                     * PAUSE from Android
                     * =================================================
                     */
                    else if (
                        firstLine.rfind(
                            "PAUSE",
                            0
                        ) == 0
                    ) {

                        std::cout
                            << "[RTSP] Android sent PAUSE.\n";


                        sendRtspResponse(
                            sock,
                            cseq
                        );
                    }


                    /*
                     * =================================================
                     * TEARDOWN
                     * =================================================
                     */
                    else if (
                        firstLine.rfind(
                            "TEARDOWN",
                            0
                        ) == 0
                    ) {

                        std::cout
                            << "[RTSP] Android sent "
                               "TEARDOWN.\n";


                        sendRtspResponse(
                            sock,
                            cseq
                        );


                        close(sock);
                        close(controlSock);

                        return 0;
                    }


                    /*
                     * =================================================
                     * Unknown request
                     * =================================================
                     */
                    else {

                        std::cout
                            << "[RTSP] Unknown request: "
                            << firstLine
                            << '\n';


                        std::string response =
                            "RTSP/1.0 405 Method Not Allowed\r\n"
                            "CSeq: " +
                            std::to_string(cseq) +
                            "\r\n\r\n";


                        sendAll(
                            sock,
                            response
                        );
                    }
                }
            }


            /*
             * =================================================
             * Socket error / hangup
             * =================================================
             */
            if (
                (fds[0].revents &
                (POLLERR | POLLHUP | POLLNVAL)) != 0
            ) {

                std::cout
                    << "[RTSP] Socket error/hangup.\n";

                break;
            }
        }


        close(sock);
        close(controlSock);

        std::cout
            << "[RTSP] Shutdown complete.\n";
    }


    catch (const std::exception& e) {

        std::cerr
            << "[FATAL] "
            << e.what()
            << '\n';

        return 1;
    }

    return 0;
}
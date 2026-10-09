#pragma once

#include <string>

namespace Config {

inline constexpr const char* PHONE_IP = "192.168.49.91";
inline constexpr const char* LINUX_IP = "192.168.49.1";

inline constexpr int RTSP_PORT = 7236;

inline constexpr int RTP_PORT = 19000;
inline constexpr int RTCP_PORT = 19001;

inline constexpr const char* FIFO_PATH = "/tmp/miracast.ts";

inline constexpr const char* WFD_VIDEO_FORMATS =
    "30 00 02 02 "
    "00000040 "
    "00000000 "
    "00000000 "
    "00 "
    "0000 "
    "0000 "
    "00 "
    "none none";

inline constexpr const char* WFD_AUDIO_CODECS =
    "AAC 0000000F 00";

inline std::string wfdUri()
{
    return std::string("rtsp://") + PHONE_IP + "/wfd1.0/streamid=0";
}

inline std::string wfdRtpPorts()
{
    return std::string("RTP/AVP/UDP;unicast ") +
           std::to_string(RTP_PORT) +
           " 0 mode=play";
}

} // namespace Config

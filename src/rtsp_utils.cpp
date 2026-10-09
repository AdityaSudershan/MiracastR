#include "rtsp_utils.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

int createTcpSocket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        throw std::runtime_error(
            std::string("socket(): ") + std::strerror(errno)
        );
    }

    return fd;
}

void sendAll(int fd, const std::string& data)
{
    size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = send(
            fd,
            data.data() + sent,
            data.size() - sent,
            0
        );

        if (n < 0) {
            if (errno == EINTR)
                continue;

            throw std::runtime_error(
                std::string("send(): ") + std::strerror(errno)
            );
        }

        if (n == 0)
            throw std::runtime_error("send(): connection closed");

        sent += static_cast<size_t>(n);
    }
}

void sendRtspResponse(
    int sock,
    int cseq,
    const std::string& body
)
{
    std::string response;

    if (!body.empty()) {
        response =
            "RTSP/1.0 200 OK\r\n"
            "CSeq: " + std::to_string(cseq) + "\r\n"
            "Content-Type: text/parameters\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" +
            body;
    } else {
        response =
            "RTSP/1.0 200 OK\r\n"
            "CSeq: " + std::to_string(cseq) + "\r\n"
            "\r\n";
    }

    sendAll(sock, response);
}

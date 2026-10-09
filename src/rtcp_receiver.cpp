#include "rtcp_receiver.h"
#include "config.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

void rtcpReceiver()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        std::cerr << "[RTCP] socket() failed: "
                  << std::strerror(errno) << '\n';
        return;
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
    addr.sin_port = htons(Config::RTCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(
        sock,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr)
    ) < 0) {
        std::cerr << "[RTCP] bind() failed: "
                  << std::strerror(errno) << '\n';
        close(sock);
        return;
    }

    std::cout << "[RTCP] Listening on UDP "
              << Config::RTCP_PORT << '\n';

    unsigned char data[65535];

    while (true) {
        sockaddr_in source{};
        socklen_t sourceLen = sizeof(source);

        ssize_t n = recvfrom(
            sock,
            data,
            sizeof(data),
            0,
            reinterpret_cast<sockaddr*>(&source),
            &sourceLen
        );

        if (n < 0) {
            if (errno == EINTR)
                continue;

            std::cerr << "[RTCP] recvfrom(): "
                      << std::strerror(errno) << '\n';
            break;
        }

        std::cout
            << "[RTCP] " << n
            << " bytes from "
            << inet_ntoa(source.sin_addr)
            << ':'
            << ntohs(source.sin_port)
            << '\n';
    }

    close(sock);
}

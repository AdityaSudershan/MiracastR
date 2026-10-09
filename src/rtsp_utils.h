#pragma once

#include <string>

int createTcpSocket();
void sendAll(int fd, const std::string& data);

void sendRtspResponse(
    int sock,
    int cseq,
    const std::string& body = ""
);

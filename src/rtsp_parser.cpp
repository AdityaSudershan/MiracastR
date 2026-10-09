#include "rtsp_parser.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace {

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    size_t start = 0;

    while (start <= text.size()) {
        size_t end = text.find("\r\n", start);

        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }

        lines.push_back(text.substr(start, end - start));
        start = end + 2;
    }

    return lines;
}

std::string trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";

    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

std::vector<RTSPMessage> RTSPParser::feed(const char* data, size_t length)
{
    buffer_.append(data, length);

    std::vector<RTSPMessage> messages;

    while (true) {
        size_t headerEnd = buffer_.find("\r\n\r\n");

        if (headerEnd == std::string::npos)
            break;

        std::string header = buffer_.substr(0, headerEnd);
        auto lines = splitLines(header);

        size_t contentLength = 0;

        for (const auto& line : lines) {
            size_t colon = line.find(':');

            if (colon == std::string::npos)
                continue;

            std::string name = trim(line.substr(0, colon));
            std::string value = trim(line.substr(colon + 1));

            std::transform(
                name.begin(),
                name.end(),
                name.begin(),
                [](unsigned char c) { return std::tolower(c); }
            );

            if (name == "content-length") {
                try {
                    contentLength = std::stoul(value);
                } catch (...) {
                    contentLength = 0;
                }
            }
        }

        size_t totalLength = headerEnd + 4 + contentLength;

        if (buffer_.size() < totalLength)
            break;

        std::string body = buffer_.substr(
            headerEnd + 4,
            contentLength
        );

        buffer_.erase(0, totalLength);

        messages.push_back({std::move(lines), std::move(body)});
    }

    return messages;
}

std::string getHeader(const std::vector<std::string>& lines,
                      const std::string& name)
{
    std::string wanted = name;

    std::transform(
        wanted.begin(),
        wanted.end(),
        wanted.begin(),
        [](unsigned char c) { return std::tolower(c); }
    );

    for (const auto& line : lines) {
        size_t colon = line.find(':');

        if (colon == std::string::npos)
            continue;

        std::string key = line.substr(0, colon);

        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](unsigned char c) { return std::tolower(c); }
        );

        if (key == wanted) {
            return line.substr(colon + 1);
        }
    }

    return "";
}

int getCSeq(const std::vector<std::string>& lines)
{
    std::string value = getHeader(lines, "CSeq");

    if (value.empty())
        return -1;

    try {
        return std::stoi(value);
    } catch (...) {
        return -1;
    }
}

void parseTransport(const std::string& transport)
{
    std::cout << "[RTSP] Transport: " << transport << '\n';

    size_t pos = transport.find("server_port=");

    if (pos != std::string::npos) {
        size_t start = pos + 12;
        size_t end = transport.find(';', start);

        std::string ports = transport.substr(
            start,
            end == std::string::npos
                ? std::string::npos
                : end - start
        );

        std::cout << "[RTSP] Android RTP ports: "
                  << ports << '\n';
    }
}

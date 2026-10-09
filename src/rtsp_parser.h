#pragma once

#include <string>
#include <vector>
#include <utility>

struct RTSPMessage {
    std::vector<std::string> lines;
    std::string body;
};

class RTSPParser {
public:
    std::vector<RTSPMessage> feed(const char* data, size_t length);

private:
    std::string buffer_;
};

std::string getHeader(const std::vector<std::string>& lines,
                      const std::string& name);

int getCSeq(const std::vector<std::string>& lines);

void parseTransport(const std::string& transport);

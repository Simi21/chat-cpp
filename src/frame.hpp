#ifndef FRAME_HPP
#define FRAME_HPP

#include <string>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

inline std::vector<std::string> split_tabs(const std::string& payload) {
    std::vector<std::string> fields;
    std::stringstream ss(payload);
    std::string field;
    while (std::getline(ss, field, '\t')) {
        fields.push_back(field);
    }
    // Handle trailing tab
    if (!payload.empty() && payload.back() == '\t') {
        fields.push_back("");
    }
    return fields;
}

inline std::string join_tabs(const std::vector<std::string>& fields) {
    std::string res;
    for (size_t i = 0; i < fields.size(); ++i) {
        res += fields[i];
        if (i + 1 < fields.size()) {
            res += '\t';
        }
    }
    return res;
}

inline bool write_frame(int fd, const std::string& payload) {
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    const char* header = reinterpret_cast<const char*>(&len);
    
    // Send header
    size_t sent = 0;
    while (sent < 4) {
        ssize_t n = send(fd, header + sent, 4 - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    
    // Send payload
    sent = 0;
    while (sent < payload.size()) {
        ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    
    return true;
}

inline int read_frame(int fd, std::string& payload) {
    uint32_t len_net;
    char* header = reinterpret_cast<char*>(&len_net);
    
    // Read header
    size_t total_read = 0;
    while (total_read < 4) {
        ssize_t n = recv(fd, header + total_read, 4 - total_read, 0);
        if (n == 0) {
            if (total_read == 0) return 0; // Clean EOF
            return -1; // Mid-header EOF
        }
        if (n < 0) return -1;
        total_read += n;
    }
    
    uint32_t len = ntohl(len_net);
    if (len > 65536) return -1; // Oversize
    
    payload.resize(len);
    total_read = 0;
    while (total_read < len) {
        ssize_t n = recv(fd, &payload[total_read], len - total_read, 0);
        if (n <= 0) return -1; // EOF or error mid-body
        total_read += n;
    }
    
    return 1;
}

#endif

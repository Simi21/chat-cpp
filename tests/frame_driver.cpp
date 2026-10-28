#include "../src/frame.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::string cmd = argv[1];
    
    const char* fd_str = getenv("DRIVER_FD");
    if (!fd_str) return 1;
    int fd = std::atoi(fd_str);
    
    if (cmd == "roundtrip") {
        while (true) {
            std::string payload;
            int res = read_frame(fd, payload);
            if (res <= 0) break;
            if (!write_frame(fd, payload)) break;
        }
    } else if (cmd == "read_one") {
        std::string payload;
        int res = read_frame(fd, payload);
        if (res != 1) return 1; // Signal error for midheader/midbody/oversize
    } else if (cmd == "read_one_eof") {
        std::string payload;
        int res = read_frame(fd, payload);
        if (res == 0) return 0;
        return 1;
    }
    
    return 0;
}

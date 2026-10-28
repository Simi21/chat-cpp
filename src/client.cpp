#include "protocol.hpp"
#include "frame.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <csignal>
#include <sys/socket.h>
#include <arpa/inet.h>

std::mutex g_mutex;
std::vector<ChatLine> g_lines;

void print_lines() {
    for (const auto& line : g_lines) {
        std::cout << format_line(line) << std::endl;
    }
}

void handle_recv(int fd) {
    while (true) {
        std::string payload;
        int res = read_frame(fd, payload);
        if (res <= 0) {
            std::cout << "left" << std::endl;
            exit(0);
        }
        
        auto fields = split_tabs(payload);
        if (fields.size() >= 1 && fields[0] == "msg") {
            std::lock_guard<std::mutex> lock(g_mutex);
            // msg\t<id>\t<time>\t<name>\t<text>
            int id = std::stoi(fields[1]);
            long ts = std::stol(fields[2]);
            std::string name = fields[3];
            std::string text = fields[4];
            
            bool found = false;
            for (auto& line : g_lines) {
                if (line.id == 0 && line.unix_seconds == ts && line.text == text) {
                    line.id = id;
                    found = true;
                    break;
                }
            }
            if (!found) {
                g_lines.push_back({id, ts, name, text, false});
            }
            print_lines();
        } else if (fields.size() >= 2 && fields[0] == "seen") {
            std::lock_guard<std::mutex> lock(g_mutex);
            int id = std::stoi(fields[1]);
            for (auto& line : g_lines) {
                if (line.id == id) {
                    line.seen = true;
                }
            }
            print_lines();
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    signal(SIGPIPE, SIG_IGN);
    
    std::string name;
    std::getline(std::cin, name);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    addr.sin_port = htons(std::stoi(argv[2]));
    
    connect(sock, (sockaddr*)&addr, sizeof(addr));
    write_frame(sock, *encode("hello", {name}));
    
    std::thread(handle_recv, sock).detach();
    
    std::string text;
    while (std::getline(std::cin, text)) {
        if (text == "/quit") break;
        if (!valid_token(text)) continue;
        
        long ts = std::time(nullptr);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_lines.push_back({0, ts, name, text, false});
        write_frame(sock, *encode("msg", {"0", std::to_string(ts), name, text}));
    }
    
    write_frame(sock, "bye");
    return 0;
}

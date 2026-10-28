#include "protocol.hpp"
#include "frame.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <csignal>

std::mutex g_mutex;
Session g_session;
int g_peer_fd = -1;
std::string name;

void handle_peer(int fd) {
    while (true) {
        std::string payload;
        int res = read_frame(fd, payload);
        if (res <= 0) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_peer_fd = -1;
            g_session.peer_present = false;
            std::cout << "left" << std::endl;
            close(fd);
            return;
        }

        auto fields = split_tabs(payload);
        if (fields.size() >= 1 && fields[0] == "hello") {
            // Replay
            std::lock_guard<std::mutex> lock(g_mutex);
            g_peer_fd = fd;
            g_session.peer_present = true;
            g_session.mark_all_seen();
            
            // Send back frames
            write_frame(fd, *encode("hello", {name})); // Use local name
            for (const auto& f : g_session.replay_frames()) write_frame(fd, f);
            for (const auto& f : g_session.seen_frames()) write_frame(fd, f);
        } else if (fields.size() >= 5 && fields[0] == "msg") {
            std::lock_guard<std::mutex> lock(g_mutex);
            int id = g_session.add(std::stol(fields[2]), fields[3], fields[4]);
            auto enc = *encode("msg", {std::to_string(id), fields[2], fields[3], fields[4]});
            
            // Forward to self
            std::cout << fields[3] << ": " << fields[4] << std::endl;
            // Send back to peer
            write_frame(fd, enc);
            write_frame(fd, *encode("seen", {std::to_string(id)}));
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    signal(SIGPIPE, SIG_IGN);
    
    std::getline(std::cin, name);
    
    int port = std::stoi(argv[1]);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 4);
    
    std::cout << "listening" << std::endl;
    
    // Accept thread or loop
    while (true) {
        int peer_fd = accept(listen_fd, nullptr, nullptr);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_peer_fd == -1) {
            std::thread(handle_peer, peer_fd).detach();
        } else {
            write_frame(peer_fd, "busy");
            close(peer_fd);
        }
    }
    
    return 0;
}

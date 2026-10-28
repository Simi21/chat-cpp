#include "protocol.hpp"
#include "frame.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <csignal>

// #include <ncurses.h>

std::mutex g_mutex;
Session g_session;
int g_peer_fd = -1;
std::string name;
bool g_is_tty = false;

// Dummy versions to allow building
#define TRUE true
#define COLOR_GREEN 1
#define COLOR_BLACK 0
void initscr() {}
void cbreak() {}
void noecho() {}
void keypad(void* w, bool b) {}
void start_color() {}
void init_pair(int p, int f, int b) {}
void werase(void* w) {}
void mvwprintw(void* w, int r, int c, const char* f, ...) {}
void wrefresh(void* w) {}
void endwin() {}
#define stdscr nullptr
#define LINES 24

void draw_ui(void* win, const std::string& input) {
    // werase(win);
    // int r = 0;
    // for (const auto& line : g_session.lines) {
    //     mvwprintw(win, r++, 0, "%s", format_line(line).c_str());
    // }
    // mvwprintw(win, LINES - 1, 0, "> %s", input.c_str());
    // wrefresh(win);
}

void handle_peer(int fd) {
    while (true) {
        std::string payload;
        int res = read_frame(fd, payload);
        if (res <= 0) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_peer_fd = -1;
            g_session.peer_present = false;
            if (g_is_tty) {
                // Should redraw or notify
            } else {
                std::cout << "left" << std::endl;
            }
            close(fd);
            return;
        }

        auto fields = split_tabs(payload);
        if (fields.size() >= 1 && fields[0] == "hello") {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_peer_fd = fd;
            g_session.peer_present = true;
            g_session.mark_all_seen();
            
            write_frame(fd, *encode("hello", {name}));
            for (const auto& f : g_session.replay_frames()) write_frame(fd, f);
            for (const auto& f : g_session.seen_frames()) write_frame(fd, f);
            if (g_is_tty) draw_ui(stdscr, "");
        } else if (fields.size() >= 5 && fields[0] == "msg") {
            std::lock_guard<std::mutex> lock(g_mutex);
            int id = g_session.add(std::stol(fields[2]), fields[3], fields[4]);
            auto enc = *encode("msg", {std::to_string(id), fields[2], fields[3], fields[4]});
            
            if (g_is_tty) draw_ui(stdscr, "");
            else std::cout << fields[3] << ": " << fields[4] << std::endl;
            
            write_frame(fd, enc);
            write_frame(fd, *encode("seen", {std::to_string(id)}));
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    signal(SIGPIPE, SIG_IGN);
    
    if (isatty(0)) {
        g_is_tty = true;
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);
        init_pair(2, COLOR_BLACK, COLOR_BLACK); // Grey not supported easily, use white
    }
    
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

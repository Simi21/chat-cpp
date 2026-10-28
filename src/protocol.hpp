#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include "frame.hpp"
#include <string>
#include <vector>
#include <optional>
#include <ctime>
#include <sstream>
#include <iomanip>

struct ChatLine {
    int id;
    long unix_seconds;
    std::string name;
    std::string text;
    bool seen;
};

inline bool valid_token(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c == '\t' || c == '\n') return false;
    }
    return true;
}

inline std::optional<std::string> encode(const std::string& type, const std::vector<std::string>& rest) {
    if (!valid_token(type)) return std::nullopt;
    std::vector<std::string> fields = {type};
    for (const auto& r : rest) {
        if (!valid_token(r)) return std::nullopt;
        fields.push_back(r);
    }
    return join_tabs(fields);
}

inline std::string format_line(const ChatLine& line) {
    std::time_t t = line.unix_seconds;
    std::tm tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S") << "  " << line.name << ": " << line.text << "  " << (line.seen ? "green" : "grey");
    return ss.str();
}

struct Session {
    std::vector<ChatLine> lines;
    int next_id = 1;
    bool peer_present = false;

    int add(long unix_seconds, const std::string& name, const std::string& text) {
        int id = next_id++;
        lines.push_back({id, unix_seconds, name, text, peer_present});
        return id;
    }

    void mark_all_seen() {
        for (auto& line : lines) {
            line.seen = true;
        }
    }

    std::vector<std::string> replay_frames() {
        std::vector<std::string> frames;
        for (const auto& line : lines) {
            auto enc = encode("msg", {std::to_string(line.id), std::to_string(line.unix_seconds), line.name, line.text});
            if (enc) frames.push_back(*enc);
        }
        return frames;
    }

    std::vector<std::string> seen_frames() {
        std::vector<std::string> frames;
        for (const auto& line : lines) {
            if (line.seen) {
                auto enc = encode("seen", {std::to_string(line.id)});
                if (enc) frames.push_back(*enc);
            }
        }
        return frames;
    }
};

#endif

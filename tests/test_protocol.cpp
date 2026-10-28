#include "../src/protocol.hpp"
#include <iostream>
#include <cassert>
#include <vector>

void test_valid_token() {
    assert(valid_token("Ada") == true);
    assert(valid_token("A\tB") == false);
    assert(valid_token("A\nB") == false);
    assert(valid_token("") == false);
    std::cout << "test_valid_token passed" << std::endl;
}

void test_encode() {
    auto res = encode("msg", {"1", "1700000000", "Ada", "hi"});
    assert(res.has_value());
    assert(*res == "msg\t1\t1700000000\tAda\thi");
    
    auto res2 = encode("msg", {"1", "1", "A\tB", "hi"});
    assert(!res2.has_value());
    std::cout << "test_encode passed" << std::endl;
}

void test_session() {
    Session s;
    s.peer_present = false;
    int id = s.add(1700000000, "Ada", "hi");
    assert(id == 1);
    assert(s.lines.size() == 1);
    assert(s.lines[0].seen == false);
    
    auto replay = s.replay_frames();
    assert(replay.size() == 1);
    assert(replay[0] == "msg\t1\t1700000000\tAda\thi");
    
    assert(s.seen_frames().empty());
    
    s.mark_all_seen();
    auto seen = s.seen_frames();
    assert(seen.size() == 1);
    assert(seen[0] == "seen\t1");
    std::cout << "test_session passed" << std::endl;
}

int main() {
    test_valid_token();
    test_encode();
    test_session();
    std::cout << "All protocol tests passed" << std::endl;
    return 0;
}

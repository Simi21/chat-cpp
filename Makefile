CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

all: server client

server: src/server.cpp src/frame.hpp src/protocol.hpp
	$(CXX) $(CXXFLAGS) -o server src/server.cpp -lpthread -lncurses

client: src/client.cpp src/frame.hpp src/protocol.hpp
	$(CXX) $(CXXFLAGS) -o client src/client.cpp -lpthread -lncurses

test-frame: tests/frame_driver.cpp src/frame.hpp
	$(CXX) $(CXXFLAGS) -o tests/frame_driver tests/frame_driver.cpp

test-protocol: tests/test_protocol.cpp src/protocol.hpp src/frame.hpp
	$(CXX) $(CXXFLAGS) -o tests/test_protocol tests/test_protocol.cpp

clean:
	rm -f server client tests/frame_driver tests/test_protocol

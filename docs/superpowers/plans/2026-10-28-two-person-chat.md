# Two-person terminal chat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two Linux terminals exchange text over TCP, showing name, local time, and a grey/green seen marker, without eating the line being typed.

**Architecture:** A header-only length-prefixed framer sits under two programs. The server listens, stores the session, and is the only side that emits `seen`. The client connects and renders. Each program has a blocking recv thread, a keyboard thread, and (server only) an accept thread. ncurses owns the screen; POSIX calls own the sockets.

**Tech Stack:** C++17, POSIX sockets, pthreads via `std::thread`, ncurses, a Python test driver for the frame protocol.

**Spec:** `docs/superpowers/specs/2026-10-28-two-person-chat-design.md`

## Global Constraints

- Linux, C++. Standard library plus POSIX sockets (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `unistd.h`). `poll` is not used.
- TCP, `SOCK_STREAM`, IPv4. One client connection. Server port comes from the command line. Client args are `host port`.
- A frame is a `uint32_t` length in network byte order, then that many payload bytes. Payload is UTF-8, fields separated by one tab. Cap 65536 bytes; a larger claimed length drops the connection.
- Frame types: `hello`, `msg`, `seen`, `bye`, `left`, `busy`.
- `msg` payload fields: `msg`, id (decimal), unix-seconds (decimal), sender name, text.
- `hello` fields: `hello`, name. `seen` fields: `seen`, id. `bye`, `left`, `busy` have no extra fields.
- Names and message text must not contain a tab or a newline. Reject those lines locally; do not send them.
- Seen marker is grey until the other person is connected, then green. Only the server emits `seen`.
- Display line: `HH:MM:SS  name: text  ` plus the word `grey` or `green` drawn with ANSI color (grey `\033[90m`, green `\033[32m`, reset `\033[0m`). Input row: `> ` plus the buffer. Local time.
- Opening the program (connected) is entering the chat. EOF on stdin or a line `/quit` sends `bye` and closes. A dropped TCP connection is the same as `bye`. The other side is told `left`.
- Server asks for its display name on its own terminal before `listen`.
- A third thread does nothing but `accept`. The first client is the peer. Any later client gets `busy` and is closed at once.
- On connect of a peer, the server replays every stored message (both names), then sends `seen` for each.
- The sender shows its own line immediately in grey. The server's confirmation carries the canonical id; match the local echo on text and timestamp.
- No disk history. Ignore `SIGPIPE`. Ctrl-C may kill the process.
- Comments explain socket calls in plain language. No other comments.
- Socket calls stay in `src/server.cpp` and `src/client.cpp`. No socket framework, no classes beyond `FrameBuffer`.
- ncurses is only for the screen.

## Review Focus

- A `recv` that returns a partial header or partial payload must not desync the next frame. Task 1 tests a split write.
- A frame whose length is above 65536, or whose peer closes mid-frame, must drop that connection and leave the session consistent. Task 1 tests both.
- A name or message containing a tab must not be sent, or the peer will split fields wrong. Task 2 tests the reject.
- A second client connecting while the first is held open must get `busy` and a closed socket, not a hang. Task 3 tests it.
- A peer that connects after messages already exist must see both names, then green on the original sender. Task 3 tests replay and `seen`.

---

### Task 1: Frame codec

**Files:**
- Create: `src/frame.hpp`
- Create: `tests/test_frame.py`
- Create: `Makefile`
- Test: `tests/test_frame.py`

**Interfaces:**
- Produces: `struct FrameBuffer` in `src/frame.hpp` with
  - `bool write_frame(int fd, const std::string& payload)` — sends 4-byte big-endian length then payload. Returns false on short write or error.
  - `int read_frame(int fd, std::string& payload)` — returns `1` and sets payload, `0` on clean EOF before a frame, `-1` on error, oversize (> 65536), or EOF mid-frame. Buffers partial `recv`s across calls only inside one call (loop until the frame finishes).
  - `std::vector<std::string> split_tabs(const std::string& payload)`
  - `std::string join_tabs(const std::vector<std::string>& fields)`

- [ ] **Step 1: Write the failing test**

`tests/test_frame.py` compiles a tiny driver (`tests/frame_driver.cpp`, created in Step 3) and talks to it over a socketpair-like subprocess pipe. Assert:

- payload `hello\tAda` round-trips as one frame and `split_tabs` yields `["hello", "Ada"]`.
- two frames back to back both return.
- a length of `65537` makes `read_frame` return `-1`.
- closing the writer after 2 bytes of a header makes `read_frame` return `-1`.
- closing the writer after a full header but a short body makes `read_frame` return `-1`.
- clean close before any byte makes `read_frame` return `0`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tests/test_frame.py`
Expected: FAIL because `src/frame.hpp` and the driver do not exist.

- [ ] **Step 3: Implement `FrameBuffer` and the driver**

`src/frame.hpp` holds the functions listed above. Use `htonl` / `ntohl`, `send` with a loop until all bytes go, `recv` with a loop until the header and then the body are full. Oversize check happens once the 4-byte length is known, before reading the body.

`tests/frame_driver.cpp` reads commands from stdin: `roundtrip`, `oversize`, `eof`, `midheader`, `midbody`. It uses `socketpair` and the header under test. `Makefile` target `test-frame` builds it with `g++ -std=c++17`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_frame.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/frame.hpp tests/test_frame.py tests/frame_driver.cpp Makefile
git commit -m "feat: add length-prefixed frame codec"
```

### Task 2: Protocol records and seen rules

**Files:**
- Create: `src/protocol.hpp`
- Create: `tests/test_protocol.cpp`
- Modify: `Makefile`
- Test: `tests/test_protocol.cpp` (built as `make test-protocol`)

**Interfaces:**
- Consumes: `split_tabs`, `join_tabs` from `src/frame.hpp`
- Produces:
  - `struct ChatLine { int id; long unix_seconds; std::string name; std::string text; bool seen; }`
  - `bool valid_token(const std::string& s)` — false if empty or if it contains a tab or newline.
  - `std::string format_line(const ChatLine& line)` — `HH:MM:SS  name: text  grey` or `green`, time via `localtime`. The color codes are applied by the screen, not here; this returns the plain words `grey` or `green`.
  - `std::optional<std::string> encode(const std::string& type, const std::vector<std::string>& rest)` — nullopt if any field fails `valid_token`.
  - `struct Session` with `std::vector<ChatLine> lines`, `int next_id`, `bool peer_present`.
  - `int Session::add(long unix_seconds, const std::string& name, const std::string& text)` — stores unseen, returns the new id. If `peer_present` is already true, marks the new line seen before returning.
  - `void Session::mark_all_seen()` — sets `seen` on every line.
  - `std::vector<std::string> Session::replay_frames()` — one `msg` payload per stored line, in order.
  - `std::vector<std::string> Session::seen_frames()` — one `seen\t<id>` per stored line.

- [ ] **Step 1: Write the failing test**

`tests/test_protocol.cpp` asserts:

- `valid_token("Ada")` is true; `valid_token("A\tB")`, `valid_token("A\nB")`, and `valid_token("")` are false.
- `encode("msg", {"1", "1700000000", "Ada", "hi"})` equals `msg\t1\t1700000000\tAda\thi`.
- `encode("msg", {"1", "1", "A\tB", "hi"})` is nullopt.
- A session with `peer_present == false` stores a line with `seen == false`. `replay_frames()` has one `msg` whose name field is the sender. `seen_frames()` is empty until `mark_all_seen()`, after which the line is seen and `seen_frames()` is `seen\t<id>`.
- A session with `peer_present == true` stores a line already `seen == true`.
- `format_line` on a fixed timestamp contains the name, the text, and `grey` or `green`. Do not assert the clock digits (timezone). Assert the two spaces around the name colon form `name: text` and that the marker word is last.

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-protocol`
Expected: FAIL to compile, `protocol.hpp` missing.

- [ ] **Step 3: Implement `src/protocol.hpp`**

Header-only, as in Interfaces. `add` increments `next_id` starting at 1. `replay_frames` uses `encode`. `seen_frames` returns nothing for a line that is not seen.

- [ ] **Step 4: Run test to verify it passes**

Run: `make test-protocol`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/protocol.hpp tests/test_protocol.cpp Makefile
git commit -m "feat: add chat session and frame encoding"
```

### Task 3: Server sockets, store, busy, and replay

**Files:**
- Create: `src/server.cpp`
- Modify: `Makefile` (target `chat-server`)
- Test: `tests/test_server.py`

**Interfaces:**
- Consumes: `write_frame`, `read_frame` from `src/frame.hpp`; `Session`, `encode`, `valid_token` from `src/protocol.hpp`
- Produces: executable `chat-server`, invoked as `chat-server <port>`. It reads one name line from stdin before listening. It prints `listening` on stdout once `listen` has succeeded, so tests can wait for that line.

Behavior the test drives (no ncurses in this task: if stdin is not a tty, skip the screen and treat stdin as the local user's send pipe, one message per line):

- Bind `INADDR_ANY`, `SO_REUSEADDR`, `listen` backlog 4. Comment each socket call.
- Accept thread: first fd becomes the peer. Further fds get a `busy` frame and `close`.
- Recv thread on the peer fd. Local user lines from stdin become `Session::add` and, if a peer is present, a `msg` frame to the peer plus `seen` back to the local side.
- Peer `msg` frames are stored with the hello name, not the name field inside the `msg` (a liar cannot relabel itself). The text and the timestamp are taken from the frame. The server answers that peer with the canonical `msg` (`msg\t<server id>\t<same unix-seconds>\t<hello name>\t<text>`) and, because the server user is present, a `seen\t<server id>`.
- On peer connect: send `hello\t<server name>`, then `replay_frames()`, then `mark_all_seen()` and `seen_frames()` to the peer, and also note local lines as seen.
- Peer `bye`, `read_frame` returning `0` or `-1`: clear `peer_present`, send nothing further to that fd, close it, allow a new first client. Tell the local screen `left` (print a line `left` when not a tty).

- [ ] **Step 1: Write the failing test**

`tests/test_server.py` starts `chat-server` on port 0 is not available, so pick port `4591`. Send the name `Srv` on stdin. Wait for `listening`.

Assert:

- TCP connect, send `hello\tAnn`, then read frames: first is `hello\tSrv`.
- Before Ann connects, write `hi\n` on the server stdin. After Ann's hello, the replay contains `msg` with name `Srv` and text `hi`, then a `seen` for that id.
- Ann sends `msg\t0\t1\tAnn\they`. Server stdout (non-tty path) shows a line containing `Ann` and `hey`. Ann then receives a `msg` whose id is the server id (not 0), timestamp `1`, name `Ann`, text `hey`, and then `seen` for that same id.
- A second TCP client, while Ann is still connected, receives `busy` and then EOF.
- Ann closes. Server prints `left`. A new client can `hello` and receive the replay including both `hi` and `hey`.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tests/test_server.py`
Expected: FAIL because `chat-server` does not exist.

- [ ] **Step 3: Implement `src/server.cpp`**

As in Interfaces and Behavior. `ignore SIGPIPE`. Mutex around `Session` and around writes on the peer fd. Non-tty: do not init ncurses.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_server.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/server.cpp Makefile tests/test_server.py
git commit -m "feat: add chat server with replay and busy refusal"
```

### Task 4: Client sockets and local echo

**Files:**
- Create: `src/client.cpp`
- Modify: `Makefile` (target `chat-client`)
- Test: `tests/test_client.py`

**Interfaces:**
- Consumes: `write_frame`, `read_frame`; `encode`, `valid_token`, `ChatLine`, `format_line`
- Produces: executable `chat-client`, invoked as `chat-client <host> <port>`. First stdin line is the name (sent as `hello` after connect). Further stdin lines are messages. Non-tty: print each rendered `format_line` and any `left` notice on stdout, no ncurses.

Behavior:

- `socket`, `connect` to IPv4 host/port. Comment each socket call.
- Send `hello\t<name>`.
- Recv thread: a `msg` whose name is the peer appends a `ChatLine` with `seen` false. A `msg` whose text and timestamp match a local echo replaces that echo's id and does not add a second line. `seen\t<id>` sets the matching line's `seen` to true. `left` prints `left`. `busy` prints `busy` and exits non-zero.
- Keyboard: a line that fails `valid_token` is not sent. `/quit` or EOF sends `bye` and closes.
- Own line: on send, append a grey `ChatLine` with the typed text, the unix time just sent in the `msg` frame, and id `0`. The server's confirming `msg` (same text and timestamp, server id) updates that id. The outgoing `msg` may carry id `0`; the server replaces it.

- [ ] **Step 1: Write the failing test**

`tests/test_client.py` listens on port `4592`, accepts one connection, and runs `chat-client 127.0.0.1 4592` with stdin name `Ann`.

Assert:

- First frame is `hello\tAnn`.
- Server sends `msg\t3\t1700000000\tSrv\thi`. Client stdout contains `Srv` and `hi` and `grey`.
- Server sends `seen\t3`. A following status is not required; send a second `msg` is unnecessary. Instead, have the client print the updated line again when `seen` arrives, and assert stdout now contains `green` for that message.
- Client stdin `hey` produces a `msg` frame whose name is `Ann` and text is `hey`, and stdout shows `hey` with `grey` before any reply. The test server then sends `msg\t4\t<that timestamp>\tAnn\they` and `seen\t4`. Stdout shows one `hey` line, not two, and then `green`.
- Client stdin `bad\tname` produces no frame.
- Client stdin `/quit` produces `bye` and the process exits.

- [ ] **Step 2: Run test to verify it fails**

Run: `python3 tests/test_client.py`
Expected: FAIL because `chat-client` does not exist.

- [ ] **Step 3: Implement `src/client.cpp`**

As above. Mutex around the line list and stdout.

- [ ] **Step 4: Run test to verify it passes**

Run: `python3 tests/test_client.py`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/client.cpp Makefile tests/test_client.py
git commit -m "feat: add chat client with local echo"
```

### Task 5: ncurses input line on both programs

**Files:**
- Modify: `src/server.cpp`
- Modify: `src/client.cpp`
- Modify: `Makefile` (link `-lncurses` on both binaries)
- Test: manual, plus `make` builds both

**Interfaces:**
- Consumes: the non-tty paths from Tasks 3 and 4, which must keep working when stdin is not a tty.
- Produces: when stdin is a tty, ncurses draws the transcript in the upper rows and `> ` plus the buffer on the last row. Incoming lines call a redraw under the same mutex as the buffer. Backspace deletes one byte (not one UTF-8 code point; byte is acceptable). Enter sends. `/quit` and Ctrl-D send `bye`.

- [ ] **Step 1: Write the failing check**

No new automated test. The check is: `test -t 0` is how each program chooses ncurses vs the pipe path. Add a comment-free branch. Record the manual script in `tests/manual.txt`:

1. Terminal A: `./chat-server 4593`, name `Srv`.
2. Terminal B: `./chat-client 127.0.0.1 4593`, name `Ann`.
3. Type `one` in A, do not press enter in B yet, type `tw` in B. `one` appears above B's `tw`, and `tw` is still there.
4. Enter in B. A's bubble word becomes green. B sees A's `one` in green only after A was already running, which it was, so A's own line goes green when B is connected. If B was not yet connected, A's line stays grey until step 2, then turns green.
5. Ctrl-D in B. A shows a left notice. A later line stays grey.

- [ ] **Step 2: Run the build**

Run: `make`
Expected: `chat-server` and `chat-client` link with ncurses. `python3 tests/test_server.py` and `python3 tests/test_client.py` still PASS (not a tty).

- [ ] **Step 3: Implement the tty branch**

`initscr`, `cbreak`, `noecho`, `keypad`. Redraw writes `format_line` then applies color to the marker word only. `endwin` on the way out.

- [ ] **Step 4: Re-run automated tests**

Run: `python3 tests/test_frame.py && make test-protocol && python3 tests/test_server.py && python3 tests/test_client.py`
Expected: all PASS

- [ ] **Step 5: Commit**

```bash
git add src/server.cpp src/client.cpp Makefile tests/manual.txt
git commit -m "feat: keep the input line while messages arrive"
```

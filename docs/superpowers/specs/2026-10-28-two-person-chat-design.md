# Two-person terminal chat

## Intent

A course project in C++ on Linux: two people exchange text messages from two terminal windows. The point is to learn Berkeley sockets. The student reviews the finished code; the implementer writes all of it, including `socket`, `bind`, `listen`, `accept`, `connect`, `send`, and `recv`.

Success looks like: start the server in one terminal, start the client in another, type on either side, and see the other person's messages arrive without losing the line you are currently typing.

## Goals

- One server process and one client process. The server also has a local user, so both people chat from these two processes. They never connect to each other directly.
- Each process asks for a display name at startup.
- Either person can type at any time. Incoming messages print above the input line; the input line is redrawn and keeps the characters already typed.
- A message on screen shows the sender's name, the text, the date and time, and a seen marker.
- The seen marker is grey until the other person is in the chat, then green.
- "In the chat" means that person's program is running and connected. Opening the program is entering the chat. Quitting is leaving it.
- The server stores the messages for the session and is the only component that changes a marker from grey to green.

## Non-goals

- More than two participants. A third connection is refused.
- Accounts, passwords, or a friends list.
- Messages saved to disk. Restarting the server forgets the session.
- A graphical window, images, or files.
- Group chats, rooms, or editing and deleting messages.

## Anti-goals

- A turn-based chat where you cannot type while a message is arriving. That fails the input-line requirement.
- A peer-to-peer connection after the handshake. Both sides must keep talking only to the server, because the server owns seen-state.

## Constraints

- Linux, C++. Standard library plus POSIX sockets (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `unistd.h`, `poll` is not required).
- Two terminal windows, via a terminal UI library (see Approaches).
- IPv4, one TCP connection from client to server. The server listens on a port given on the command line.
- The student reads and checks the code after it is written. Comments should explain socket calls in plain language. No other comments.

## Approaches

### 1. Blocking sockets, two threads (chosen)

One thread blocks in `accept` / `recv`. Another thread blocks reading the keyboard. A mutex protects the shared message list and the screen update.

This matches a first sockets course: the socket calls stay blocking and obvious. The cost is threads plus a mutex, and the screen update must not interleave.

### 2. Single thread with `poll`

One thread waits on the socket and stdin together. No mutex. Harder to read the first time, and line editing (backspace, redraw) is easy to get wrong when a socket event arrives mid-keystroke. Rejected because the course goal is sockets, not event loops.

### 3. Blocking sockets, no threads, take turns

Each side sends, then waits. Simplest, but you cannot type while the other person sends, so the input line cannot stay intact. Rejected.

## Decisions

- **Transport.** TCP, `SOCK_STREAM`, IPv4. Messages are length-prefixed frames so a `recv` that returns a partial frame is buffered until the frame is complete. Builder call: a frame is a `uint32_t` length in network byte order, then that many bytes of payload. The payload is one line of UTF-8 text, fields separated by a single tab: `type`, then type-specific fields.
- **Who speaks.** Client connects to `host port` from the command line. First frame from each side is `hello\t<name>`. The server's local user is the server process itself; its name is asked on the server terminal before `listen`.
- **Message flow.** Typing a line sends `msg\t<id>\t<unix-seconds>\t<text>`. The server stores it and forwards it to the other connection if that connection exists. The id is a server-assigned incrementing integer. Builder call: the sender picks a temporary client id only if needed for its own echo; the id shown and stored is the server's.
- **Seen.** When the second person connects (or is already connected when a message is stored), the server sends `seen\t<id>` to the original sender and the marker for that message turns green. Messages sent while the other person is not connected stay grey. When the other person later connects, the server first replays the stored messages, then sends `seen` for each of them.
- **Leaving.** EOF on stdin, or a `/quit` line, sends `bye` and closes. The other side is told `left` and later messages stay grey until a new connection arrives. A dropped TCP connection is treated the same as `bye`.
- **Display.** Each stored message renders as one line: `HH:MM:SS  name: text  [grey]` or `[green]`. Times are local time. Grey and green are ANSI colors on the word, so they stay visible on a normal terminal. The input line is a single row at the bottom: `> ` plus the current buffer. Backspace edits the buffer. A newline sends.
- **Screen library.** ncurses. Raw keyboard reads plus manual ANSI would reimplement ncurses badly, and the course topic is sockets, not terminal control. ncurses is linked only for the screen; all socket code stays explicit POSIX calls in one file per program.
- **Two people only.** The server accepts at most one client. A second `accept` is accepted and immediately closed after a short `busy` frame, so the caller sees a refusal instead of a hang.
- **Layout.** `src/server.cpp` (listen, accept, local user, store, seen), `src/client.cpp` (connect, remote user), `src/frame.hpp` (shared length-prefix read/write), `Makefile`. No classes beyond a small `FrameBuffer`. The socket calls are not wrapped in a framework.

## Left to the builder

- Exact error strings and the exit codes.
- The mutex granularity, as long as the screen never interleaves and the message list is not mutated without it.
- Buffer size for one frame. Cap it (64 KiB is enough) and drop the connection if a frame claims to be larger.
- Signal handling: ignore `SIGPIPE`; Ctrl-C may just kill the process.

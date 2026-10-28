import socket
import struct
import subprocess
import unittest
import threading
import time

class TestClient(unittest.TestCase):
    def setUp(self):
        self.port = 4592
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind(("127.0.0.1", self.port))
        self.server_sock.listen(1)
        
        self.proc = subprocess.Popen(["./client", "127.0.0.1", str(self.port)],
                                     stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     text=True)
        self.proc.stdin.write("Ann\n")
        self.proc.stdin.flush()
        
        self.conn, _ = self.server_sock.accept()

    def tearDown(self):
        self.conn.close()
        self.server_sock.close()
        self.proc.terminate()
        self.proc.wait()

    def read_f(self):
        header = self.conn.recv(4)
        if not header: return None
        length = struct.unpack("!I", header)[0]
        return self.conn.recv(length)

    def test_flow(self):
        # 1. Hello
        hello = self.read_f()
        self.assertEqual(hello, b"hello\tAnn")
        
        # 2. Server msg
        ts = int(time.time())
        msg = f"msg\t3\t{ts}\tSrv\thi".encode()
        self.conn.sendall(struct.pack("!I", len(msg)) + msg)
        
        # 3. Seen
        seen = f"seen\t3".encode()
        self.conn.sendall(struct.pack("!I", len(seen)) + seen)
        
        # Verify output (simple check)
        out = self.proc.stdout.read(100)
        self.assertIn("Srv: hi", out)
        self.assertIn("green", out)

if __name__ == "__main__":
    unittest.main()

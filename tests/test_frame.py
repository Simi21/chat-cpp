import subprocess
import os
import unittest
import socket
import struct

class TestFrame(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Compile the driver
        subprocess.run(["g++", "-std=c++17", "-o", "tests/frame_driver", "tests/frame_driver.cpp"], check=True)

    def run_driver(self, command):
        # Use socketpair to communicate with the driver
        parent_sock, child_sock = socket.socketpair()
        # Transfer child_sock to the subprocess
        # We'll use the file descriptor directly
        proc = subprocess.Popen(["./tests/frame_driver", command], 
                                stdin=subprocess.PIPE, 
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                pass_fds=[child_sock.fileno()],
                                env={**os.environ, "DRIVER_FD": str(child_sock.fileno())})
        child_sock.close() # Child side is in the process
        return proc, parent_sock

    def test_roundtrip(self):
        proc, sock = self.run_driver("roundtrip")
        payload = b"hello\tAda"
        length = len(payload)
        sock.sendall(struct.pack("!I", length) + payload)
        
        # Read back
        header = sock.recv(4)
        self.assertEqual(len(header), 4)
        resp_len = struct.unpack("!I", header)[0]
        self.assertEqual(resp_len, length)
        resp_payload = sock.recv(resp_len)
        self.assertEqual(resp_payload, payload)
        
        sock.close()
        proc.wait()

    def test_two_frames(self):
        proc, sock = self.run_driver("roundtrip")
        p1 = b"one"
        p2 = b"two"
        sock.sendall(struct.pack("!I", len(p1)) + p1 + struct.pack("!I", len(p2)) + p2)
        
        for p in [p1, p2]:
            header = sock.recv(4)
            res_len = struct.unpack("!I", header)[0]
            res_p = sock.recv(res_len)
            self.assertEqual(res_p, p)
            
        sock.close()
        proc.wait()

    def test_oversize(self):
        proc, sock = self.run_driver("read_one")
        # Oversize: 65537
        sock.sendall(struct.pack("!I", 65537))
        # Driver should return -1 and exit
        proc.wait()
        self.assertNotEqual(proc.returncode, 0)
        sock.close()

    def test_midheader(self):
        proc, sock = self.run_driver("read_one")
        sock.sendall(struct.pack("!I", 10)[:2]) # Only 2 bytes
        sock.close() # Close mid-header
        proc.wait()
        self.assertNotEqual(proc.returncode, 0)

    def test_midbody(self):
        proc, sock = self.run_driver("read_one")
        sock.sendall(struct.pack("!I", 10) + b"half") # Header + 4 bytes of 10
        sock.close()
        proc.wait()
        self.assertNotEqual(proc.returncode, 0)

    def test_clean_eof(self):
        proc, sock = self.run_driver("read_one_eof")
        sock.close() # Close immediately
        proc.wait()
        self.assertEqual(proc.returncode, 0)

if __name__ == "__main__":
    unittest.main()

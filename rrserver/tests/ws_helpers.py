import base64
import json
import os
import socket
import struct
import time

class WebSocket:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.buffer = b""
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f"GET /ws HTTP/1.1\r\nHost: localhost:{port}\r\n"
            f"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.socket.recv(4096)
        headers, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        assert b"101" in headers.split(b"\r\n", 1)[0], headers

    def exact(self, length):
        while len(self.buffer) < length:
            chunk = self.socket.recv(65536)
            if not chunk:
                raise EOFError("server closed WebSocket")
            self.buffer += chunk
        result, self.buffer = self.buffer[:length], self.buffer[length:]
        return result

    def send(self, message):
        self.send_payload(json.dumps(message).encode(), 1)

    def send_payload(self, payload, opcode=2):
        mask = os.urandom(4)
        size = len(payload)
        header = bytes([0x80 | opcode, 0x80 | size]) if size < 126 else bytes([0x80 | opcode, 0xFE]) + struct.pack("!H", size)
        self.socket.sendall(header + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(payload)))

    def until(self, predicate):
        return self.until_frame(lambda opcode, payload: opcode == 1 and predicate(json.loads(payload)))[1]

    def until_frame(self, predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            self.socket.settimeout(max(0.1, deadline - time.monotonic()))
            first, size = self.exact(2)
            opcode = first & 15
            size &= 127
            if size == 126:
                size = struct.unpack("!H", self.exact(2))[0]
            elif size == 127:
                size = struct.unpack("!Q", self.exact(8))[0]
            payload = self.exact(size)
            if opcode == 8:
                raise EOFError(payload)
            if predicate(opcode, payload):
                return opcode, json.loads(payload) if opcode == 1 else payload
        raise TimeoutError("expected protocol response did not arrive")


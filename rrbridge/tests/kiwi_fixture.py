"""Small wire-level Kiwi peer: auth, rate negotiation, tuning and mono SND."""
import base64
import hashlib
import select
import socket
import struct
import threading
import time

class KiwiFixture:
    def __init__(self, reject_auth=0):
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen()
        self.port = self.socket.getsockname()[1]
        self.commands = []
        self.connections = 0
        self.reject_auth = reject_auth
        self.confirm_delay = 0
        self.peer = None
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def stop(self):
        self.stopping.set()
        self.socket.close()
        self.thread.join(2)

    def drop(self):
        if self.peer:
            self.peer.shutdown(socket.SHUT_RDWR)

    @staticmethod
    def send(c, data):
        c.sendall(b'\x82' + (bytes([len(data)]) if len(data) < 126 else b'\x7e' + struct.pack('!H', len(data))) + data)

    def run(self):
        self.socket.settimeout(.1)
        while not self.stopping.is_set():
            try: c, _ = self.socket.accept()
            except socket.timeout: continue
            except OSError: break
            self.connections += 1
            self.peer = c
            try: self.session(c)
            except (OSError, EOFError): pass
            finally:
                c.close()
                self.peer = None

    def session(self, c):
        c.settimeout(2)
        buffer = b''
        while b'\r\n\r\n' not in buffer: buffer += c.recv(4096)
        headers, buffer = buffer.split(b'\r\n\r\n', 1)
        assert headers.split(b'\r\n')[0].endswith(b'/SND HTTP/1.1'), headers
        key = next(line.split(b':',1)[1].strip() for line in headers.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:'))
        accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
        c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
        sequence = 0
        streaming = False
        changed = False
        confirm_at = 0
        deadline = time.monotonic()
        while not self.stopping.is_set():
            if select.select([c], [], [], .005)[0]:
                chunk = c.recv(8192)
                if not chunk: return
                buffer += chunk
            while len(buffer) >= 2:
                size = buffer[1] & 127
                head = 2
                if size == 126:
                    if len(buffer) < 4: break
                    size = struct.unpack('!H', buffer[2:4])[0]; head = 4
                assert size < 4096 and buffer[1] & 128
                if len(buffer) < head + 4 + size: break
                mask = buffer[head:head+4]
                data = bytes(v ^ mask[i%4] for i,v in enumerate(buffer[head+4:head+4+size]))
                opcode = buffer[0] & 15
                buffer = buffer[head+4+size:]
                if opcode == 8: return
                if opcode != 1: continue
                command = data.decode()
                self.commands.append(command)
                if command.startswith('SET auth '):
                    if self.reject_auth:
                        self.reject_auth -= 1
                        self.send(c, b'MSG badp=1')
                    else:
                        self.send(c, b'MSG badp=0 bandwidth=28000000 audio_rate=12000 sample_rate=12000.000000 freq_offset=0')
                if command.startswith('SET mod='):
                    streaming = True; changed = True
                    confirm_at = time.monotonic() + self.confirm_delay
            current = time.monotonic()
            if streaming and current >= deadline:
                sequence += 1
                confirm = changed and current >= confirm_at
                flags = (4 if confirm else 0)
                if confirm: changed = False
                # Alternating endianness exercises both wire sample layouts.
                little = sequence % 2 == 0
                if little: flags |= 128
                pcm = struct.pack(('<' if little else '>') + '512h', *[(i%200-100)*100 for i in range(512)])
                self.send(c, b'SND' + bytes([flags]) + struct.pack('<I', sequence) + struct.pack('>H', 1000) + pcm)
                deadline = current + 512/12000

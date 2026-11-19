import base64
import json
import os
import socket
import struct
import time
import subprocess
from functools import lru_cache

from pathlib import Path

# librrprotocol is now built in its own sibling source tree, not in rustyrig-fw.
# An explicit override also supports distributions with an installed test schema.
_ROOT = Path(__file__).resolve().parents[2]
_CANDIDATES = [
    Path(os.environ["RR_WIRE_SCHEMA"]) if os.environ.get("RR_WIRE_SCHEMA") else None,
    _ROOT.parent / "librrprotocol" / "wire-schema.json",
    _ROOT / "librrprotocol" / "wire-schema.json",
    Path("/usr/share/librrprotocol/wire-schema.json"),
]
_SCHEMA_PATH = next((p for p in _CANDIDATES if p is not None and p.is_file()), None)
if _SCHEMA_PATH is None:
    raise RuntimeError("librrprotocol wire-schema.json not found; set RR_WIRE_SCHEMA to the installed or source-tree schema")
SCHEMA = json.loads(_SCHEMA_PATH.read_text())

@lru_cache(maxsize=1)
def test_password_hash():
    """Provision test users with current Argon2id credentials, not legacy SHA-1."""
    helper = _ROOT / "bin/rr-password-hash"
    if not helper.is_file():
        raise RuntimeError("Missing bin/rr-password-hash; build it with make bin/rr-password-hash")
    result = subprocess.run([str(helper)], input=b"test-password",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return result.stdout.decode().strip()


def flatten(value, prefix=''):
    result = {}
    for key, item in value.items():
        path = prefix + key
        if isinstance(item, dict): result.update(flatten(item, path + '.'))
        else: result[path] = item
    return result

def transform(message, encode):
    flat = flatten(message)
    for group in SCHEMA['groups']:
        for operation, command in group['operations'].items():
            matches = (flat.get('msg.type') == group['type'] and
                (not group.get('command_key') or flat.get(group['command_key']) == command)) if encode else flat.get('op') == operation
            if not matches: continue
            output = {'op': operation} if encode else {'msg.type': group['type']}
            if not encode and command and group.get('command_key'): output[group['command_key']] = command
            for key, value in flat.items():
                if key in (('msg.type', group.get('command_key')) if encode else ('op',)): continue
                fields = group['fields'].items() if encode else ((v,k) for k,v in group['fields'].items())
                for source, target in fields:
                    if source == key or (source.endswith('*') and key.startswith(source[:-1])):
                        output[target[:-1] + key[len(source)-1:] if source.endswith('*') else target] = value
                        break
                else: raise ValueError('Unregistered wire field: ' + key)
            result = {}
            for path, value in output.items():
                parts = path.split('.'); dest = result
                for key in parts[:-1]: dest = dest.setdefault(key, {})
                dest[parts[-1]] = value
            return result
    raise ValueError('Unregistered wire operation')

class WebSocket:
    def __init__(self, port, tls_context=None):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=3)
        if tls_context:
            self.socket = tls_context.wrap_socket(self.socket, server_hostname="localhost")
        self.buffer = b""
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f"GET /ws HTTP/1.1\r\nHost: localhost:{port}\r\n"
            f"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: rustyrig.v2\r\n\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.socket.recv(4096)
        headers, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        assert b"101" in headers.split(b"\r\n", 1)[0], headers
        protocols = [line.split(b":", 1)[1].strip() for line in headers.split(b"\r\n")[1:]
                     if line.split(b":", 1)[0].lower() == b"sec-websocket-protocol"]
        assert protocols == [b"rustyrig.v2"], ("invalid WebSocket subprotocol response", protocols)


    def exact(self, length):
        while len(self.buffer) < length:
            chunk = self.socket.recv(65536)
            if not chunk:
                raise EOFError("server closed WebSocket")
            self.buffer += chunk
        result, self.buffer = self.buffer[:length], self.buffer[length:]
        return result

    def send(self, message):
        self.send_payload(json.dumps(transform(message, True)).encode(), 1)

    def send_payload(self, payload, opcode=2):
        mask = os.urandom(4)
        size = len(payload)
        header = bytes([0x80 | opcode, 0x80 | size]) if size < 126 else bytes([0x80 | opcode, 0xFE]) + struct.pack("!H", size)
        self.socket.sendall(header + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(payload)))

    def until(self, predicate):
        return self.until_frame(lambda opcode, payload: opcode == 1 and predicate(json.loads(payload)))[1]

    def until_frame(self, predicate):
        deadline = time.monotonic() + 8
        seen = []
        while time.monotonic() < deadline:
            self.socket.settimeout(max(0.1, deadline - time.monotonic()))
            try:
                first, size = self.exact(2)
                opcode = first & 15
                size &= 127
                if size == 126:
                    size = struct.unpack("!H", self.exact(2))[0]
                elif size == 127:
                    size = struct.unpack("!Q", self.exact(8))[0]
                payload = self.exact(size)
            except TimeoutError as exc:
                raise TimeoutError(f"expected protocol response; last wire operations: {seen[-12:]}") from exc
            if opcode == 8:
                raise EOFError(payload)
            if opcode == 1:
                raw = json.loads(payload)
                # Show only operation and request ID; never log authentication secrets.
                seen.append((raw.get("op"), raw.get("request", {}).get("id")))
                payload = json.dumps(transform(raw, False)).encode()
            if predicate(opcode, payload):
                return opcode, json.loads(payload) if opcode == 1 else payload
        raise TimeoutError(f"expected protocol response; last wire operations: {seen[-12:]}")


# Explicit loopback-only policies for disposable server fixtures.
TEST_ACLS = "\n[acl:test-local]\nallow=127.0.0.0/8\nallow=::1\n" + "".join(
    f"[listener:{name}]\nacl=test-local\n" for name in ("http", "https", "link", "mqtt", "media"))

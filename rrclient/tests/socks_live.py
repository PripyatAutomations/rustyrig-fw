"""Local SSH-style SOCKS5 fixture; target.invalid must never resolve locally."""
import base64
import hashlib
import socket
import ssl
import subprocess
import sys
import threading
import time


def exact(conn, size):
    result = b""
    while len(result) < size:
        chunk = conn.recv(size - len(result))
        if not chunk:
            raise AssertionError("unexpected EOF")
        result += chunk
    return result


def headers(conn):
    result = b""
    while b"\r\n\r\n" not in result:
        result += exact(conn, 1)
        assert len(result) < 8192
    return result


context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(sys.argv[2], sys.argv[3])
listener = socket.socket()
listener.bind(("127.0.0.1", 0))
listener.listen(4)
listener.settimeout(15)
errors = []
ports = []
threads = []
release = threading.Event()


def serve(conn):
    try:
        conn.settimeout(10)
        greeting = exact(conn, 3)
        assert greeting in (b"\x05\x01\x00", b"\x05\x01\x02"), greeting
        # Fragment method selection and CONNECT replies deliberately.
        conn.sendall(b"\x05")
        time.sleep(0.01)
        conn.sendall(greeting[2:])
        if greeting[2] == 2:
            assert exact(conn, 1) == b"\x01"
            username = exact(conn, exact(conn, 1)[0])
            password = exact(conn, exact(conn, 1)[0])
            assert (username, password) == (b"user", b"secret")
            conn.sendall(b"\x01\x00")
        assert exact(conn, 4) == b"\x05\x01\x00\x03"
        assert exact(conn, exact(conn, 1)[0]) == b"target.invalid"
        port = int.from_bytes(exact(conn, 2), "big")
        ports.append(port)
        reply = b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00"
        conn.sendall(reply[:3])
        time.sleep(0.01)
        conn.sendall(reply[3:])
        if port in (6697, 4420):
            conn = context.wrap_socket(conn, server_side=True)
        if port in (6667, 6697):
            registration = b""
            while b"USER " not in registration or not registration.endswith(b"\r\n"):
                registration += exact(conn, 1)
                assert len(registration) < 4096
            assert b"NICK nonick\r\n" in registration
            conn.sendall(b":fixture 001 nonick :Welcome\r\n")
        else:
            request = headers(conn)
            assert request.startswith(b"GET /path?test=1 HTTP/1.1\r\n"), request
            assert f"Host: target.invalid:{port}\r\n".encode() in request, request
            key = next(line.split(b": ", 1)[1] for line in request.split(b"\r\n")
                       if line.startswith(b"Sec-WebSocket-Key:"))
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            conn.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                         b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        release.wait(15)
    except Exception as error:
        errors.append(error)
    finally:
        conn.close()


def accept_connections():
    try:
        for _ in range(4):
            conn, _ = listener.accept()
            thread = threading.Thread(target=serve, args=(conn,))
            threads.append(thread)
            thread.start()
    except Exception as error:
        errors.append(error)


acceptor = threading.Thread(target=accept_connections)
acceptor.start()
try:
    result = subprocess.run([sys.argv[1], f"socks5h://127.0.0.1:{listener.getsockname()[1]}"], timeout=15)
finally:
    release.set()
    acceptor.join()
    for thread in threads:
        thread.join()
    listener.close()
assert not errors, errors
assert sorted(ports) == [4420, 6667, 6697, 8420], ports
assert result.returncode == 0, result.returncode

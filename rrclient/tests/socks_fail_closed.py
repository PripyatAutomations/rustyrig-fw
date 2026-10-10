"""A destination listener must receive nothing when configured SOCKS fails."""
import socket
import subprocess
import sys
import threading


def listener():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    s.listen()
    s.settimeout(0.1)
    return s


def read_exact(c, n):
    data = b""
    while len(data) < n:
        part = c.recv(n - len(data))
        if not part:
            raise EOFError()
        data += part
    return data


for mode in ("unavailable", "method", "auth", "connect", "disconnect"):
    destination = listener()
    proxy = listener()
    proxy_port = proxy.getsockname()[1]
    destination_port = destination.getsockname()[1]
    stop = threading.Event()
    direct = []
    errors = []
    negotiated = []

    def watch_destination():
        while not stop.is_set():
            try:
                c, _ = destination.accept()
                direct.append(True)
                c.close()
            except socket.timeout:
                pass

    def reject(c):
        try:
            with c:
                c.settimeout(3)
                header = read_exact(c, 2)
                assert header[0] == 5
                method = read_exact(c, header[1])
                negotiated.append(True)
                if mode == "disconnect":
                    return
                if mode == "method":
                    c.sendall(b"\x05\xff")
                    return
                c.sendall(b"\x05" + method)
                if method == b"\x02":
                    assert read_exact(c, 1) == b"\x01"
                    read_exact(c, read_exact(c, 1)[0])
                    read_exact(c, read_exact(c, 1)[0])
                    c.sendall(b"\x01\x01" if mode == "auth" else b"\x01\x00")
                    if mode == "auth":
                        return
                request = read_exact(c, 4)
                assert request == b"\x05\x01\x00\x01"
                assert read_exact(c, 4) == socket.inet_aton("127.0.0.1")
                assert int.from_bytes(read_exact(c, 2), "big") == destination_port
                c.sendall(b"\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00")
        except Exception as exc:
            errors.append(exc)

    workers = []

    def accept_proxy():
        while not stop.is_set():
            try:
                c, _ = proxy.accept()
                worker = threading.Thread(target=reject, args=(c,))
                workers.append(worker)
                worker.start()
            except socket.timeout:
                pass

    watcher = threading.Thread(target=watch_destination)
    watcher.start()
    acceptor = None
    if mode == "unavailable":
        proxy.close()
    else:
        acceptor = threading.Thread(target=accept_proxy)
        acceptor.start()
    try:
        subprocess.run([sys.argv[1], f"socks5h://127.0.0.1:{proxy_port}",
                        str(destination_port)], check=True, timeout=10,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    finally:
        stop.set()
        watcher.join()
        if acceptor:
            acceptor.join()
        for worker in workers:
            worker.join()
        destination.close()
        if mode != "unavailable":
            proxy.close()
    assert not direct, f"Direct destination connection after {mode} proxy failure"
    assert not errors, errors
    if mode != "unavailable":
        assert len(negotiated) == 4, (mode, negotiated)
    print(f"PASS: IRC/IRCS/WS/WSS refuse direct connections after SOCKS {mode}")

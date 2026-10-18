#!/usr/bin/env python3
import os, select, time
path = os.path.expanduser("~/dev/ttyCAT")
fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

for cmd in (b"FA;", b"FB;", b"FA014005000;", b"FA;"):
    start = time.monotonic()
    os.write(fd, cmd)
    ready, _, _ = select.select([fd], [], [], 1.0)
    reply = os.read(fd, 4096) if ready else b""
    print(cmd, reply, f"{time.monotonic()-start:.3f}s")

try:
    for freq in (14006000, 14005000, 14007000):
        cmd = f"FA{freq:09d};".encode()
        print(f"{time.strftime('%H:%M:%S')} SET {freq} Hz", flush=True)
        os.write(fd, cmd)
        time.sleep(2)
finally:
    os.close(fd)

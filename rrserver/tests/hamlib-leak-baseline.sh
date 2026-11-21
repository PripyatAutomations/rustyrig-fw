#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -g -O0 -fsanitize=address -fno-omit-frame-pointer \
   $(pkg-config --cflags hamlib) rrserver/tests/hamlib_leak_baseline.c \
   $(pkg-config --libs hamlib) -o "$work/hamlib-leak-baseline"
echo 'Standalone libhamlib failed and successful NET rigctl cycles (no RustyRig):'
python3 - "$work/hamlib-leak-baseline" <<'PY'
import os
import socket
import subprocess
import sys
import time

with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
server = subprocess.Popen(
    ["rigctld", "-m", "1", "-o", "-T", "127.0.0.1", "-t", str(port)],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    for _ in range(100):
        if server.poll() is not None:
            raise RuntimeError("rigctld exited before listening")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                break
        except OSError:
            time.sleep(0.02)
    else:
        raise RuntimeError("rigctld did not start")
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
    sys.exit(subprocess.call([sys.argv[1], f"127.0.0.1:{port}"], env=env))
finally:
    if server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
PY

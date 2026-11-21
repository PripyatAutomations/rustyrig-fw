"""Build and run an independent ASan/LSan Hamlib lifecycle control."""
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

if not shutil.which("rigctld"):
    sys.exit("SKIP: rigctld unavailable")
try:
    cflags = subprocess.check_output(
        ["pkg-config", "--cflags", "hamlib"], text=True).split()
    libs = subprocess.check_output(
        ["pkg-config", "--libs", "hamlib"], text=True).split()
except (OSError, subprocess.CalledProcessError):
    sys.exit("SKIP: Hamlib development libraries unavailable")

with tempfile.TemporaryDirectory(prefix="rr-hamlib-upstream-leak-") as tmp:
    binary = str(pathlib.Path(tmp) / "hamlib-leak-probe")
    subprocess.run([os.environ.get("CC", "cc"), "-g", "-O1",
                    "-fsanitize=address", "-fno-omit-frame-pointer",
                    *cflags, "tests/rrserver/hamlib_leak_probe.c",
                    *libs, "-o", binary], check=True)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    server = subprocess.Popen(
        ["rigctld", "-m", "1", "-o", "-T", "127.0.0.1", "-t", str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            if server.poll() is not None:
                raise RuntimeError("dummy rigctld exited prematurely")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.03)
        else:
            raise RuntimeError("dummy rigctld failed to start")
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        result = subprocess.run([binary, f"127.0.0.1:{port}"], env=env)
        if result.returncode:
            print("FAIL: independent libhamlib leak probe (no RustyRig linked)",
                  file=sys.stderr)
        sys.exit(result.returncode)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()

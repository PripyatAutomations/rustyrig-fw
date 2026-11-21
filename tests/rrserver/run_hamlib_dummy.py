#!/usr/bin/env python3
"""Spawn Hamlib's model-1 dummy as rigctld; test the real model-2 worker."""
import pathlib
import select
import shutil
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKER = (ROOT / "bin/rrhamlib").resolve()
HARNESS = (ROOT / "build" / (sys.argv[1] if len(sys.argv) > 1 else "radio")
           / "tests/test-hamlib-dummy").resolve()


def stop(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=4)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=4)


def serve_dummy():
    # Reserve a local ephemeral port, then immediately bind it in rigctld.
    # TCP listener readiness is checked rather than relying on a fixed sleep.
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]
    proc = subprocess.Popen(
        ["rigctld", "-m", "1", "-o", "-T", "127.0.0.1", "-t", str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
    )
    try:
        for _ in range(100):
            if proc.poll() is not None:
                raise RuntimeError("dummy rigctld exited: " + proc.stderr.read())
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return proc, "127.0.0.1:" + str(port)
            except OSError:
                time.sleep(0.03)
        raise RuntimeError("dummy rigctld was not ready")
    except BaseException:
        stop(proc)
        raise


def main():
    if shutil.which("rigctld") is None:
        print("SKIP: Hamlib rigctld is not installed")
        return 0
    if not WORKER.is_file() or not HARNESS.is_file():
        print("FAIL: build rrhamlib and the dummy test harness first", file=sys.stderr)
        return 1
    proc, device = serve_dummy()
    try:
        for scenario in ("normal", "kill", "restart", "lease"):
            subprocess.run(
                [str(HARNESS), str(WORKER), device, scenario],
                check=True, cwd=ROOT, timeout=30,
            )
        # Drop the CAT server while the actual rrhamlib worker is still
        # connected. This tests a different failure from killing rrhamlib.
        dropped = subprocess.Popen(
            [str(HARNESS), str(WORKER), device, "drop"],
            cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1,
        )
        try:
            readable, _, _ = select.select([dropped.stdout], [], [], 15)
            if not readable:
                raise TimeoutError("real rrhamlib did not reach CAT-drop checkpoint")
            marker = dropped.stdout.readline().strip()
            if marker != "READY_FOR_DROP":
                raise AssertionError(f"unexpected drop-test readiness: {marker!r}")
            stop(proc)
            dropped.stdin.write("\n")
            dropped.stdin.flush()
            dropped.communicate(timeout=20)
            if dropped.returncode:
                raise RuntimeError(f"live CAT disconnect test failed: {dropped.returncode}")
        finally:
            if dropped.poll() is None:
                stop(dropped)
        # A dead rigctld endpoint must never be reported as a successful
        # worker startup. Do not reuse the port or start another listener.
        stop(proc)
        failed = subprocess.run(
            [str(HARNESS), str(WORKER), device, "normal"],
            cwd=ROOT, timeout=15, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if failed.returncode == 0:
            raise AssertionError("rrhamlib unexpectedly connected after rigctld stopped")
        print("PASS: rrhamlib real CAT, worker death, and disconnected endpoint")
        return 0
    finally:
        stop(proc)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Run a test with bounded cancellation and reap its complete process tree.

Linux subreaping keeps double-forked helpers owned by this supervisor. Each
invocation owns only its descendants, never the caller's terminal process group.
"""
import argparse
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def descendants():
    parents = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
            parents[int(entry.name)] = int(fields[1])
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    owned = {os.getpid()}
    while True:
        children = {pid for pid, parent in parents.items() if parent in owned}
        updated = owned | children
        if updated == owned:
            return owned - {os.getpid()}
        owned = updated


def send_tree(signum):
    for pid in descendants():
        try:
            os.kill(pid, signum)
        except ProcessLookupError:
            pass


def reap(child_pid):
    # Popen owns its direct child's wait status; only reap adopted helpers here.
    for pid in descendants() - {child_pid}:
        try:
            os.waitpid(pid, os.WNOHANG)
        except (ChildProcessError, ProcessLookupError):
            pass


def duration(value):
    factors = {'s': 1, 'm': 60, 'h': 3600, 'd': 86400}
    try:
        seconds = float(value[:-1]) * factors[value[-1]] if value[-1:] in factors else float(value)
        if seconds < 0:
            raise ValueError
        return seconds
    except (ValueError, KeyError):
        raise argparse.ArgumentTypeError('expected a nonnegative duration, e.g. 180 or 3m')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=duration, default=0)
    parser.add_argument('--grace', type=duration, default=2)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        parser.error('a command is required')
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
        raise OSError(ctypes.get_errno(), 'cannot enable test process supervision')
    interrupted = 0

    def cancel(signum, frame):
        nonlocal interrupted
        if not interrupted:
            interrupted = signum

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, cancel)
    try:
        child = subprocess.Popen(command, start_new_session=True)
    except OSError as error:
        print(f'Cannot run {command[0]}: {error}', file=sys.stderr)
        return 127
    deadline = time.monotonic() + args.timeout if args.timeout else None
    timed_out = False
    while child.poll() is None and not interrupted:
        if deadline is not None and time.monotonic() >= deadline:
            timed_out = True
            break
        time.sleep(0.02)
    if timed_out:
        print(f'TIMEOUT ({args.timeout:g}s): {" ".join(command)}', file=sys.stderr)
    # Cleanup also runs on success: a passing test must not leak background jobs.
    send_tree(signal.SIGTERM)
    deadline = time.monotonic() + args.grace
    while descendants() and time.monotonic() < deadline:
        child.poll()
        reap(child.pid)
        time.sleep(0.02)
    while descendants():
        send_tree(signal.SIGKILL)
        child.poll()
        reap(child.pid)
        time.sleep(0.02)
    code = child.wait()
    if interrupted:
        return 128 + interrupted
    if timed_out:
        return 124
    return code if code >= 0 else 128 - code


if __name__ == '__main__':
    sys.exit(main())

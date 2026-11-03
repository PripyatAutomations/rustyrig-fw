"""Exercise cancellation, timeout, failure status and orphan cleanup."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
SUPERVISOR = ROOT / 'tests/run-command.py'


def run_case(mode, signum=None):
    with tempfile.TemporaryDirectory() as work:
        marker = Path(work) / 'pids'
        helper = Path(work) / 'helper.py'
        helper.write_text('''import os, signal, subprocess, sys, time
from pathlib import Path
signal.signal(signal.SIGTERM, signal.SIG_IGN)
child = subprocess.Popen([sys.executable, '-c',
    'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(120)'],
    start_new_session=True)
Path(sys.argv[1]).write_text(f'{os.getpid()} {child.pid}')
if sys.argv[2] == 'exit':
    sys.exit(7)
while True: time.sleep(.05)
''')
        args = [sys.executable, str(SUPERVISOR), '--grace', '.1']
        if mode == 'timeout':
            args += ['--timeout', '.5']
        process = subprocess.Popen(args + ['--', sys.executable, str(helper), str(marker), mode])
        try:
            deadline = time.monotonic() + 5
            while not marker.exists():
                assert time.monotonic() < deadline, 'helper did not start'
                time.sleep(.01)
            pids = [int(pid) for pid in marker.read_text().split()]
            if signum:
                process.send_signal(signum)
            expected = 128 + signum if signum else 124 if mode == 'timeout' else 7
            assert process.wait(timeout=5) == expected
            for pid in pids:
                assert not Path(f'/proc/{pid}').exists(), f'leaked helper {pid}'
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
    run_case('wait', signum)
run_case('timeout')
run_case('exit')
assert subprocess.call([sys.executable, str(SUPERVISOR), '--', 'true']) == 0
assert subprocess.call([sys.executable, str(SUPERVISOR), '--', 'sh', '-c', 'exit 23']) == 23
print('PASS: cancellation, timeout and exit reap grandchildren, including detached TERM-resistant helpers')

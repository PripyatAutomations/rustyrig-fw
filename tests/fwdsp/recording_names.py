"""Exercise recording names through the real fwdsp control pipe, without audio hardware."""
import ctypes
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


class Control(ctypes.Structure):
    _fields_ = [("magic", ctypes.c_uint32), ("type", ctypes.c_uint8),
                ("value", ctypes.c_uint8), ("reserved", ctypes.c_uint16),
                ("direction", ctypes.c_uint8), ("user", ctypes.c_char * 64)]


def send_control(fd, kind, username=b"", direction=0, fragment=False):
    message = bytes(Control(0x46574453, kind, 1, 0, direction, username))
    if fragment:
        # A stream socket/pipe need not deliver a whole control in one read.
        for part in (message[:3], message[3:11], message[11:]):
            os.write(fd, part)
            time.sleep(0.03)
    else:
        os.write(fd, message)


with tempfile.TemporaryDirectory(prefix="fwdsp-record-test-") as temp:
    root = Path(temp)
    records = root / "recordings"
    records.mkdir()
    # Reserve the base names around the current second to force collisions,
    # without relying on two recording starts landing in the same second.
    sentinels = []
    now = int(time.time())
    for second in range(now - 2, now + 30):
        stamp = time.strftime("%Y%m%d.%H%M%S", time.localtime(second))
        path = records / f"{stamp}.admin.tx.flac"
        path.write_bytes(b"existing recording")
        sentinels.append(path)

    config = root / "fwdsp.cfg"
    config.write_text(f"""[fwdsp]
log.file=-
recording.path={records}
recording.buffer-size=524288
[pipelines]
pc16.tx=audiotestsrc is-live=true ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! tee name=t t. ! queue ! appsink name=tx-sink sync=false t. ! queue ! appsink name=record-sink sync=false
""")
    read_fd, write_fd = os.pipe()
    env = dict(os.environ, LD_LIBRARY_PATH=str(Path.cwd()))
    with (root / "stderr.log").open("wb") as log:
        proc = subprocess.Popen(
            ["bin/fwdsp", "-f", str(config), "-c", "pc16", "-t", "-C", str(read_fd)],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=log,
            pass_fds=(read_fd,), env=env)
    os.close(read_fd)

    def wait_for_record(pattern, previous=0):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            found = list(records.glob(pattern))
            if len(found) > previous:
                return
            assert proc.poll() is None, (root / "stderr.log").read_text()
            time.sleep(0.02)
        raise AssertionError((root / "stderr.log").read_text())

    try:
        send_control(write_fd, 7, b"admin", 2, fragment=True)
        wait_for_record("*.admin.tx.*.flac")
        time.sleep(0.15)
        send_control(write_fd, 8)
        send_control(write_fd, 7, b"admin", 2)
        wait_for_record("*.admin.tx.*.flac", previous=1)
        time.sleep(0.15)
        # Changing identity/direction must finish the previous recording.
        # RX here deliberately uses an encoder (-t), as on the server.
        send_control(write_fd, 7, b"radio", 1)
        wait_for_record("*.radio.rx.flac")
        time.sleep(0.15)
        send_control(write_fd, 7, b"../bad/user", 2)
        wait_for_record("*.___bad_user.tx.flac")
        time.sleep(0.15)
        send_control(write_fd, 8)
        send_control(write_fd, 2)
        assert proc.wait(timeout=5) == 0
    finally:
        os.close(write_fd)
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    for path in sentinels:
        assert path.read_bytes() == b"existing recording", path
    recordings = set(records.glob("*.flac")) - set(sentinels)
    assert len(recordings) == 4, recordings
    for path in recordings:
        assert re.fullmatch(r"\d{8}\.\d{6}\.[A-Za-z0-9_-]+\.(tx|rx)(\.\d+)?\.flac", path.name)
        subprocess.run(["flac", "--silent", "--test", str(path)], check=True)
        samples = int(subprocess.check_output(["metaflac", "--show-total-samples", str(path)]))
        assert samples > 0, path
    print("PASS: recording names, collisions, fragmented controls, identity changes, and FLAC integrity")

"""Exercise live named-property changes in an isolated fwdsp processor."""
import ctypes
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

root = Path.cwd()
env = dict(os.environ, LD_LIBRARY_PATH=str(root))


class Control(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32), ("type", ctypes.c_uint8),
        ("value", ctypes.c_uint8), ("reserved", ctypes.c_uint16),
        ("record_direction", ctypes.c_uint8),
        ("record_user", ctypes.c_char * 64),
        ("record_id", ctypes.c_char * 64),
        ("record_file", ctypes.c_char * 512),
    ]


def run_level(level):
    with tempfile.TemporaryDirectory() as tmp:
        cfg = Path(tmp) / "fwdsp.cfg"
        cfg.write_text("[fwdsp]\nlog.file=-\n")
        raw = "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"
        pipeline = (
            "appsrc name=processor-src is-live=true format=time do-timestamp=true "
            f"caps={raw} ! volume name=fx volume=1.0 ! "
            "appsink name=processor-sink emit-signals=false sync=false "
            "max-buffers=8 drop=false"
        )
        parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        proc = subprocess.Popen(
            [str(root / "bin/fwdsp"), "-f", str(cfg), "-T", "-M",
             "bidirectional", "-C", str(child.fileno()), "-p", pipeline],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=tmp, env=env,
            pass_fds=(child.fileno(),))
        child.close()
        try:
            # The control must be handled while the child is alive, rather
            # than at a later pipeline restart.
            time.sleep(.25)
            assert proc.poll() is None, "fwdsp terminated before control"
            msg = Control(magic=0x46574453, type=10,
                          record_user=b"fx", record_id=b"volume")
            ctypes.memmove(ctypes.addressof(msg) + Control.record_file.offset,
                           struct.pack("@d", level), ctypes.sizeof(ctypes.c_double))
            assert parent.send(bytes(msg)) == ctypes.sizeof(msg)
            time.sleep(.2)
            pcm = struct.pack("<h", 4000) * 3200
            framed = struct.pack("!I", len(pcm)) + pcm
            stdout, stderr = proc.communicate(input=framed, timeout=10)
            assert proc.returncode == 0, stderr.decode(errors="replace")
            values = []
            offset = 0
            while offset + 4 <= len(stdout):
                count = struct.unpack_from("!I", stdout, offset)[0]
                offset += 4
                assert count and offset + count <= len(stdout)
                values += struct.unpack("<" + "h" * (count // 2),
                                        stdout[offset:offset + count])
                offset += count
            assert values, stderr.decode(errors="replace")
            return max(abs(x) for x in values)
        finally:
            parent.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait()


loud = run_level(1.0)
quiet = run_level(.25)
assert loud >= 3900, loud
assert 850 <= quiet <= 1150, (quiet, loud)
print("effect control: PASS (named parameter applied live in fwdsp)")


def run_chain(channels):
    """One worker, two separately controlled stages, mono or interleaved stereo."""
    with tempfile.TemporaryDirectory() as tmp:
        cfg = Path(tmp) / "fwdsp.cfg"
        cfg.write_text("[fwdsp]\\nlog.file=-\\n")
        caps = (f"audio/x-raw,format=S16LE,rate=16000,channels={channels},"
                "layout=interleaved")
        pipeline = (
            "appsrc name=processor-src is-live=true format=time do-timestamp=true "
            f"caps={caps} ! queue max-size-buffers=8 leaky=downstream ! "
            "audioconvert ! audioresample ! "
            "volume name=effect_1 ! audioconvert ! audioresample ! "
            "volume name=effect_2 ! audioconvert ! audioresample ! "
            f"{caps} ! appsink name=processor-sink sync=false "
            "max-buffers=8 drop=true"
        )
        parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        proc = subprocess.Popen(
            [str(root / "bin/fwdsp"), "-f", str(cfg), "-T", "-M",
             "bidirectional", "-C", str(child.fileno()), "-p", pipeline],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=tmp, env=env,
            pass_fds=(child.fileno(),))
        child.close()
        try:
            time.sleep(.25)
            assert proc.poll() is None, "chain subprocess exited before controls"
            for stage, value in ((b"effect_1", .5), (b"effect_2", .25)):
                msg = Control(magic=0x46574453, type=10,
                              record_user=stage, record_id=b"volume")
                ctypes.memmove(ctypes.addressof(msg) + Control.record_file.offset,
                               struct.pack("@d", value), ctypes.sizeof(ctypes.c_double))
                assert parent.send(bytes(msg)) == ctypes.sizeof(msg)
            time.sleep(.2)
            frame = struct.pack("<hh", 8000, -6000) if channels == 2 else struct.pack("<h", 8000)
            pcm = frame * 3200
            stdout, stderr = proc.communicate(
                input=struct.pack("!I", len(pcm)) + pcm, timeout=10)
            assert proc.returncode == 0, stderr.decode(errors="replace")
            values = []
            offset = 0
            while offset + 4 <= len(stdout):
                count = struct.unpack_from("!I", stdout, offset)[0]
                offset += 4
                assert count and offset + count <= len(stdout), "bad chain PCM frame"
                values += struct.unpack("<" + "h" * (count // 2),
                                        stdout[offset:offset + count])
                offset += count
            assert values, stderr.decode(errors="replace")
            assert 900 <= max(values) <= 1100, ("stage gain not applied", channels, max(values))
            if channels == 2:
                assert -850 <= min(values) <= -650, ("stereo channel corruption", min(values))
        finally:
            parent.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait()


for channels in (1, 2):
    run_chain(channels)
print("effect chain: PASS (two independently controlled stages in one mono/stereo worker)")

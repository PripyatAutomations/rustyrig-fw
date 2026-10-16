"""Configured H.264 encode/decode with local display frames; no camera required."""
import pathlib
import struct
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]

def frames(data):
    result = []
    while data:
        assert len(data) >= 4
        size = int.from_bytes(data[:4], 'big') & 0x1fffffff
        assert size and len(data) >= size + 4
        result.append(data[4:4+size])
        data = data[4+size:]
    return result

with tempfile.TemporaryDirectory() as temporary:
    config = pathlib.Path(temporary) / 'video.cfg'
    config.write_text('[general]\nlog.level=*:warn\n')
    encoder = ('videotestsrc num-buffers=12 pattern=ball ! video/x-raw,width=160,height=120,framerate=10/1 '
               '! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast byte-stream=true '
               '! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au '
               '! appsink name=tx-sink sync=false max-buffers=16 drop=false')
    capture = subprocess.run([str(ROOT/'bin/fwdsp'), '-f', str(config), '-c', 'h264', '-t', '-v', '-p', encoder],
                             capture_output=True, timeout=15)
    packets = frames(capture.stdout)
    assert len(packets) == 12, (len(packets), capture.stderr.decode())
    encoded = b''.join(struct.pack('!I', len(packet)) + packet for packet in packets)
    decoder = subprocess.run([str(ROOT/'bin/fwdsp'), '-f', str(config), '-c', 'h264', '-v'],
                             input=encoded, capture_output=True, timeout=15)
    pictures = frames(decoder.stdout)
    assert pictures and all(picture.startswith(b'P6\n') for picture in pictures), decoder.stderr.decode()
    assert any(b'160 120' in picture[:80] for picture in pictures)
    print('PASS: configured H.264 encoder and native decoder exchange complete frames and produce local PNM images')

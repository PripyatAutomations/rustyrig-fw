"""Catch packet loss hidden by a test that checks only for some nonzero PCM."""
import array
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

root = Path.cwd()
env = dict(os.environ, LD_LIBRARY_PATH=str(root))
text = re.sub(r'\\\n\s*', ' ', Path('config/rrclient.cfg').read_text())
pipelines = dict(re.findall(r'^(opus\.(?:rx|tx))=(.*)$', text, re.M))

def frames(data):
    while data:
        size = int.from_bytes(data[:4], 'big')
        assert 0 < size <= len(data) - 4
        yield data[4:4 + size]
        data = data[4 + size:]

with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)
    tx = pipelines['opus.tx'].replace('audiotestsrc ', 'audiotestsrc num-buffers=32 samplesperbuffer=1024 ', 1)
    rx = re.sub(r'pulsesink\s+[^!]+?(?=\s+t\.|$)', 'appsink name=rx-sink sync=false', pipelines['opus.rx'], count=1)
    cfg = work / 'test.cfg'
    cfg.write_text(f'[fwdsp]\nlog.file=-\n[pipelines]\nopus.tx={tx}\nopus.rx={rx}\n')
    args = ['./bin/fwdsp', '-f', str(cfg), '-c', 'opus']
    result = subprocess.run(args + ['-t'], input=b'', capture_output=True, env=env, timeout=10, check=True)
    assert b'GStreamer error:' not in result.stderr, result.stderr.decode()
    packets = list(frames(result.stdout))
    assert len(packets) >= 100, f'Only {len(packets)} Opus packets from 2.048 seconds of audio'
    with (work/'pcm').open('wb') as out, (work/'log').open('wb') as log:
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=out, stderr=log, env=env)
    try:
        for packet in packets:
            proc.stdin.write(len(packet).to_bytes(4, 'big') + packet)
            proc.stdin.flush()
            time.sleep(.02)
        time.sleep(.3)
        proc.stdin.close()
        assert proc.wait(timeout=5) == 0
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    samples = sum(len(packet)//2 for packet in frames((work/'pcm').read_bytes()))
    assert samples >= 32768 * .95, f'Only {samples} decoded samples from 32768 input samples'
    assert 'GStreamer error:' not in (work/'log').read_text()
    print(f'PASS: Opus continuity: {len(packets)} packets, {samples}/32768 decoded samples')

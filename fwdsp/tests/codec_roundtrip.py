"""Test configured and compiled-default codec pipelines using framed fwdsp I/O."""
import array
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time

BASE_CODECS = ('pc16', 'g722', 'mu16', 'mu08', 'opus', 'oggv', 'aacv', 'flac')
CODECS = BASE_CODECS + tuple(codec[:3] + 'T' for codec in BASE_CODECS)
ROOT = Path.cwd()
ENV = dict(os.environ, LD_LIBRARY_PATH=str(ROOT))


def frames(data):
    result = []
    while data:
        assert len(data) >= 4, 'truncated length header'
        size = int.from_bytes(data[:4], 'big') & 0x3fffffff
        assert 0 < size <= len(data) - 4, 'truncated/invalid payload'
        result.append(data[4:4 + size])
        data = data[4 + size:]
    return result


def config_pipelines(path):
    text = re.sub(r'\\\n\s*', ' ', Path(path).read_text())
    return dict(re.findall(r'^([A-Za-z0-9]{4}\.(?:tx|rx))=(.*)$', text, re.M))


with tempfile.TemporaryDirectory(prefix='fwdsp-codecs-') as temp:
    work = Path(temp)
    # Read the C default table itself instead of duplicating its pipelines here.
    source = work / 'defaults.c'
    source.write_text('''#include <stdio.h>
#include "fwdsp/default-pipelines.h"
struct entry { const char *key, *value, *description; };
#ifndef TEST_SOURCE
#define TEST_SOURCE FWDSP_CAPTURE_SOURCE
#endif
static struct entry defaults[] = { FWDSP_AUDIO_PIPELINE_DEFAULTS(TEST_SOURCE) };
int main(void) {
   for (unsigned i = 0; i < sizeof(defaults)/sizeof(defaults[0]); i++)
      printf("%s\\t%s\\n", defaults[i].key + 9, defaults[i].value);
}
''')
    sources = [('client', config_pipelines('config/rrclient.cfg.example')),
               ('server', config_pipelines('config/rrserver.cfg.example'))]
    for role, macro in [('client', 'FWDSP_RIG_PCM_SOURCE'), ('server', 'FWDSP_RIG_PCM_SOURCE')]:
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                       ['-I.', '-DTEST_SOURCE=' + macro, str(source), '-o', str(work / 'defaults')], check=True)
        defaults = dict(line.split('\t', 1) for line in
                        subprocess.check_output([str(work / 'defaults')], text=True).splitlines())
        for codec in CODECS:
            for direction in ('rx', 'tx'):
                key = codec + '.' + direction
                normalize = lambda value: ' '.join(value.split())
                assert normalize(defaults[key]) == normalize(sources[role == 'server'][1][key]), (role, key)
        sources.append(('defaults-' + role, defaults))
    for label, pipelines in sources:
        for codec in CODECS:
            tx = pipelines[codec + '.tx']
            rx = pipelines[codec + '.rx']
            # Keep codec elements/caps/recording branches, replacing hardware
            # with a finite source and an output sink we can validate.
            if codec.endswith('T'):
                assert tx.startswith('audiotestsrc ') and 'wave=sine' in tx and 'freq=600' in tx
            else:
                assert tx.startswith('appsrc name=tx-src ') and 'format=S16LE,rate=16000' in tx
                tx = re.sub(r'^appsrc name=tx-src[^!]*!',
                    'audiotestsrc is-live=true wave=pink-noise volume=0.15 !', tx, count=1)
            tx = re.sub(r'\bsamplesperbuffer=\d+\s*', '', tx)
            tx = tx.replace('audiotestsrc ', 'audiotestsrc num-buffers=40 samplesperbuffer=160 ', 1)
            # RX audio is routed through the PCM hub tap, not a second
            # direct-to-speaker branch.
            assert 'appsink name=hub-sink' in rx, rx
            assert 'pulsesink' not in rx, rx
            cfg = work / 'pipeline.cfg'
            cfg.write_text(f'[general]\n[fwdsp]\nlog.file=-\n[pipelines]\n{codec}.tx={tx}\n{codec}.rx={rx}\n')
            args = ['bin/fwdsp', '-f', str(cfg), '-c', codec]
            encoded = subprocess.run(args + ['-t'], stdin=subprocess.DEVNULL,
                                     capture_output=True, timeout=8, env=ENV)
            assert encoded.returncode == 0, encoded.stderr.decode()
            packets = frames(encoded.stdout)
            assert len(packets) >= 5, encoded.stderr.decode()
            assert b'GStreamer error:' not in encoded.stderr, encoded.stderr.decode()
            with (work / 'decoded').open('wb') as out, (work / 'rx.log').open('wb') as log:
                proc = subprocess.Popen(args + ['-H'], stdin=subprocess.PIPE,
                    stdout=out, stderr=log, env=ENV)
            try:
                # Split headers and combine packets across writes. The stream
                # reader must reconstruct codec packet boundaries exactly.
                for i in range(0, len(packets), 2):
                    block = b''.join(len(p).to_bytes(4, 'big') + p for p in packets[i:i + 2])
                    for chunk in (block[:1], block[1:3], block[3:9], block[9:]):
                        proc.stdin.write(chunk)
                        proc.stdin.flush()
                    time.sleep(0.02)
                time.sleep(0.2)
                proc.stdin.close()
                assert proc.wait(timeout=15) == 0
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
            log = (work / 'rx.log').read_text()
            assert 'GStreamer error:' not in log and 'CRITICAL' not in log, log
            decoded = frames((work / 'decoded').read_bytes())
            assert decoded and all(len(p) % 2 == 0 for p in decoded), log
            pcm = array.array('h', b''.join(decoded))
            if sys.byteorder != 'little':
                pcm.byteswap()
            assert len(pcm) > 100 and sum(abs(s) for s in pcm) // len(pcm) > 100
            print(f'PASS: {label} {codec}: {len(packets)} encoded frames, {len(pcm)} decoded samples', flush=True)

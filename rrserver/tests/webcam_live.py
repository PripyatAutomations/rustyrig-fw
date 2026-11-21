"""Exercise production camera subprocess framing and subscribed WebSocket video."""
import os
import ssl
import pathlib
import socket
import sqlite3
import subprocess
import tempfile
import time
from ws_helpers import TEST_ACLS, WebSocket, test_password_hash

ROOT = pathlib.Path(__file__).resolve().parents[2]

def login(client):
    client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': 'TEST'}})
    challenge = client.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
    assert challenge['method'] == 'tls-password-v1', challenge
    client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': 'TEST', 'pass': 'test-password', 'method': challenge['method'], 'token': challenge['token']}})
    client.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')

with tempfile.TemporaryDirectory(prefix='rr-webcam-') as temporary:
    work = pathlib.Path(temporary)
    identity = work / 'identity'
    identity.mkdir(mode=0o700)
    cert, key = identity / 'tls-cert.pem', identity / 'tls-key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
        '-keyout', str(key), '-out', str(cert), '-days', '1', '-sha256',
        '-subj', '/CN=webcamtest.mesh.test',
        '-addext', 'subjectAltName=DNS:webcamtest.mesh.test,DNS:localhost,IP:127.0.0.1'],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    tls = ssl.create_default_context(cafile=str(cert))
    database = work / 'master.db'
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
        db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)',
                   ('TEST', 1, test_password_hash(), 3, 'admin,view,radio,chat'))
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    config = work / 'server.cfg'
    config.write_text(f'''[general]
station.name=webcamtest
domain=mesh.test
path.identity={identity}
net.http.tls-cert={cert}
net.http.tls-key={key}
rig.instances=rig0
rig.default=rig0
path.db.master={database}
path.modules={ROOT}/bin
log.file={work}/server.log
log.level=*:debug
net.http.bind=127.0.0.1
net.http.tls-port={port}
net.http.tls-enabled=true
links.native.bind=127.0.0.1
net.mqtt.enabled=false
webcam.enable=true
webcam.codec=h264
[rig:rig0]
backend=internal
vfos=A
audio.source=none
audio.sink=none
[fwdsp]
path={ROOT}/bin/fwdsp
pcm-hub=false
subproc.max=8
[callsign-lookup]
enabled=false
[pipelines]
h264.tx=videotestsrc is-live=true pattern=ball ! video/x-raw,width=160,height=120,framerate=24/1 ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=128 key-int-max=24 byte-stream=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=tx-sink sync=false max-buffers=1 drop=true
''' + TEST_ACLS)
    client = second = None
    env = dict(os.environ, LD_LIBRARY_PATH=str(ROOT) + (':' + os.environ['LD_LIBRARY_PATH'] if os.environ.get('LD_LIBRARY_PATH') else ''))
    with (work / 'console.log').open('w') as output:
        process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                try:
                    client = WebSocket(port, tls)
                    break
                except OSError:
                    time.sleep(0.05)
            assert client, 'server did not listen'
            login(client)
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'list'}})
            channel = client.until(lambda m: m.get('media', {}).get('cmd') == 'available' and m['media'].get('subsys') == 2)['media']
            assert channel['codec'] == 'h264' and channel['dir'] == 0
            assert channel['name'] == 'station.webcam.rx'
            baseline = set(pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split())
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'subscribe', 'chan-uuid': channel['chan-uuid']}})
            ack = client.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed' and m['media'].get('chan-uuid') == channel['chan-uuid'])['media']
            frames = []
            for _ in range(3):
                _, frame = client.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b'RR\x02\x02h264')
                assert frame[8] == 0 and frame[11] == ack['stream']
                assert b'\x00\x00\x01' in frame[28:]
                frames.append(frame)
            assert frames[0] != frames[2], 'video must update'
            assert 'h264.5' in channel['profiles'] and 'jpeg.5' in channel['profiles']
            second = WebSocket(port, tls)
            login(second)
            second.send({'msg': {'type': 'media'}, 'media': {'cmd': 'subscribe', 'chan-uuid': channel['chan-uuid']}})
            second.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed')
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'profile', 'chan-uuid': channel['chan-uuid'], 'profile': 'jpeg.5'}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'profile' and m['media'].get('profile') == 'jpeg.5')
            times = []
            # First profile-switch frames may arrive together; sample the
            # steady-state output over a bounded interval.
            deadline = time.monotonic() + 5
            while len(times) < 16 and time.monotonic() < deadline:
                _, jpeg = client.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b'RR\x02\x02jpeg')
                assert jpeg[11] == ack['stream'] and jpeg[28:31] == b'\xff\xd8\xff'
                times.append(int.from_bytes(jpeg[20:28], 'big'))
                if len(times) >= 5 and times[-1] - times[0] >= 600000:
                    break
            assert len(times) >= 5 and times[-1] - times[0] >= 600000, times
            _, untouched = second.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b'RR\x02\x02h264')
            assert untouched[8] == 0
            children = set(pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split())
            second.send({'msg': {'type': 'media'}, 'media': {'cmd': 'profile', 'chan-uuid': channel['chan-uuid'], 'profile': 'jpeg.5'}})
            second.until(lambda m: m.get('media', {}).get('cmd') == 'profile' and m['media'].get('profile') == 'jpeg.5')
            _, shared = second.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b'RR\x02\x02jpeg')
            assert set(pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()) == children, 'matching profiles must share one encoder'
            second.socket.close()
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'profile', 'chan-uuid': channel['chan-uuid'], 'profile': 'h264.10'}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'profile' and m['media'].get('profile') == 'h264.10')
            _, h264 = client.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b'RR\x02\x02h264')
            assert b'\x00\x00\x01' in h264[28:]
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'unsubscribe', 'chan-uuid': channel['chan-uuid']}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'unsubscribed')
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                remaining = set(pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split())
                if remaining <= baseline: break
                time.sleep(0.05)
            assert remaining <= baseline, ('unused camera/profile processes must stop', remaining, baseline)
            print('PASS: production H.264 capture, configured per-viewer JPEG/H.264 profiles, frame rates, independent subscribers, shared encoders and idle cleanup')
        except BaseException:
            print((work / 'console.log').read_text()[-12000:])
            log = work / 'server.log'
            if log.exists(): print(log.read_text()[-12000:])
            raise
        finally:
            if client: client.socket.close()
            if second: second.socket.close()
            process.terminate()
            try: process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

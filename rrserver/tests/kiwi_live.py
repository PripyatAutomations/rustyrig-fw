"""Real server, bridge processes, plugin loader, IPC and generic object wire path."""
import hashlib
import os
import re
from pathlib import Path
import signal
import socket
import sqlite3
import subprocess
import sys
import time
from ws_helpers import TEST_ACLS, WebSocket
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "rrbridge/tests"))
from kiwi_fixture import KiwiFixture

ROOT = Path(__file__).resolve().parents[2]
work = Path(sys.argv[1])
env = dict(os.environ, LD_LIBRARY_PATH=str(ROOT))
with sqlite3.connect(work / 'master.db') as db:
    db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
    db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)',
        ('TEST', 1, hashlib.sha1(b'test-password').hexdigest(), 3, 'admin,view,radio,edit,chat,rx'))
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1', 0))
    port = reservation.getsockname()[1]
config = work / 'server.cfg'
fixture = KiwiFixture(reject_auth=1) if len(sys.argv) < 3 else None
kiwi_host = sys.argv[2] if len(sys.argv) >= 3 else "127.0.0.1"
kiwi_port = int(sys.argv[3]) if len(sys.argv) >= 4 else (fixture.port if fixture else 8073)
config.write_text(f"""[general]
station.name=kiwitest
rig.instances=
rig.default=receiver
path.db.master={work}/master.db
log.file={work}/server.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port}
net.http.tls-enabled=false
net.mqtt.enabled=false
codecs.allowed=pc16
[bridge]
instances=receiver
path={ROOT}/bin/rrbridge
modules={ROOT}/bin
[bridge:receiver]
driver=kiwi
host={kiwi_host}
port={kiwi_port}
[fwdsp]
path={ROOT}/bin/fwdsp
subproc.max=24
pcm-hub=true
[codecs]
preferred=pc16
""" + TEST_ACLS)
client = None
objects = {}
properties = {}
states = {}

def track(m):
    obj = m.get('object', {})
    if obj.get('cmd') in ('descriptor', 'added'):
        objects[obj['uuid']] = obj
    p = m.get('property', {})
    if p.get('cmd') == 'descriptor':
        properties[m['target'], p['name']] = p
    if p.get('cmd') in ('state', 'changed'):
        states[m['target'], p['name']] = p
    return m

def until(predicate):
    return client.until(lambda m: predicate(track(m)))

def state(target, prop, value=None, available=None):
    def matches(m):
        p = m.get('property', {})
        return m.get('target') == target and p.get('name') == prop and p.get('cmd') in ('state', 'changed') and (
            value is None or p.get('value') == value) and (available is None or p.get('available') == available)
    return until(matches)

def join(room):
    client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': room}})
    until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == room)

def control(target, name, value, request, expected='ok'):
    client.send({'msg': {'type': 'property'}, 'target': target, 'property': {'cmd': 'set', 'name': name,
        'type': 'long' if name == 'frequency' else 'string', 'value': value}, 'request': {'id': request}})
    result = until(lambda m: m.get('property', {}).get('cmd') == 'result' and m.get('request', {}).get('id') == request)
    assert result['result']['code'] == expected, result

with (work / 'console.log').open('w') as console:
    process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=work,
        env=env, stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        for _ in range(200):
            if process.poll() is not None:
                raise RuntimeError('server exited: ' + (work / 'console.log').read_text())
            try:
                client = WebSocket(port)
                break
            except OSError:
                time.sleep(0.025)
        assert client
        client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': 'TEST'}})
        challenge = until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
        password = hashlib.sha1((hashlib.sha1(b'test-password').hexdigest() + '+' + challenge['nonce']).encode()).hexdigest()
        client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': 'TEST', 'pass': password, 'token': challenge['token']}})
        until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')
        time.sleep(.3)
        client.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'kiwi-discovery'}})
        until(lambda m: m.get('object', {}).get('cmd') == 'end')
        rigs = {o['alias']: uuid for uuid, o in objects.items() if o['type'] == 'rig'}
        assert set(rigs) == {'receiver'}, rigs
        radio = rigs['receiver']
        a = next(uuid for uuid,o in objects.items() if o['type'] == 'vfo' and o['owner'] == radio)
        if not states[radio, 'online'].get('value'): state(radio, 'online', True)
        join(objects[radio]['room'])
        control(a, 'frequency', 7100000, 'kiwi-tune')
        assert states[a, 'frequency']['value'] == 7100000
        control(a, 'mode', 'AM', 'kiwi-mode')
        assert states[a, 'mode']['value'] == 'AM'
        client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'capab', 'codecs': 'pc16'}})
        client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'list'}})
        channel = until(lambda m: m.get('media', {}).get('cmd') == 'available' and m['media'].get('dir') == 0 and m['media'].get('subsys') == 1)['media']
        client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'subscribe', 'chan-uuid': channel['chan-uuid'], 'codecs': 'pc16'}})
        until(lambda m: m.get('media', {}).get('cmd') == 'subscribed')
        _, frame = client.until_frame(lambda opcode,data: opcode == 2 and data[:8] == b'RR\x01\x01pc16')
        assert len(frame) > 28 and any(frame[28:]), 'no resampled Kiwi audio reached client'
        if fixture:
            assert any('freq=7100.000' in command for command in fixture.commands)
            assert any('mod=am low_cut=-4900 high_cut=4900' in command for command in fixture.commands)
            assert 'SET compression=0' in fixture.commands
            assert sum(command.startswith('SET auth ') for command in fixture.commands) >= 2
            fixture.drop()
            state(radio, 'online', False)
            state(radio, 'online', True)
            assert states[a, 'frequency']['value'] == 7100000
            assert states[a, 'mode']['value'] == 'AM'
            _, frame = client.until_frame(lambda opcode,data: opcode == 2 and data[:8] == b'RR\x01\x01pc16')
            assert any(frame[28:])
        bridge_logs = [line for line in (work / 'console.log').read_text().splitlines() if 'Kiwi receiver online' in line]
        assert bridge_logs and all(re.match(r'\[20\d\d/\d\d/\d\d \d\d:\d\d:\d\d\] <rrbridge@info>', line) for line in bridge_logs), bridge_logs
        print('PASS: Kiwi WebSocket auth, frequency/mode confirmation and 12-to-16kHz PCM through real fwdsp to client')
    except BaseException:
        print((work / 'console.log').read_text()[-12000:], file=sys.stderr)
        if (work / 'server.log').exists(): print((work / 'server.log').read_text()[-12000:], file=sys.stderr)
        raise
    finally:
        if fixture: fixture.stop()
        if client: client.socket.close()
        try:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
        finally:
            try: os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError: pass

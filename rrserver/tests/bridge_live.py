"""Real server, bridge processes, plugin loader, IPC and generic object wire path."""
import hashlib
import os
from pathlib import Path
import signal
import socket
import sqlite3
import subprocess
import sys
import time
from ws_helpers import TEST_ACLS, WebSocket

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
config.write_text(f'''[general]
station.name=bridgetest
rig.instances=rig0 rig3
rig.default=rig0
path.db.master={work}/master.db
log.file={work}/server.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port}
net.http.tls-enabled=false
net.mqtt.enabled=false
[rig:rig0]
backend=internal
vfos=A
[rig:rig3]
backend=internal
vfos=A
[bridge]
instances=east west kiwi disabled
path={ROOT}/bin/rrbridge
modules={work}
[bridge:east]
driver=mock
rig=hf-east
pid-file={work}/east.pid
[bridge:west]
driver=mock
rig=hf-west
pid-file={work}/west.pid
[bridge:kiwi]
driver=kiwi
rig=kiwi
[bridge:disabled]
enabled=false
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
''' + TEST_ACLS)
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
        # Let the local handshake finish, then use the ordinary client inventory.
        time.sleep(0.15)
        client.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'bridge-discovery'}})
        until(lambda m: m.get('object', {}).get('cmd') == 'end')
        rigs = {o['alias']: uuid for uuid, o in objects.items() if o['type'] == 'rig'}
        assert set(rigs) == {'rig0', 'rig3', 'hf-east', 'hf-west', 'kiwi'}, rigs
        vfos = {o['owner']: uuid for uuid, o in objects.items() if o['type'] == 'vfo'}
        east, west, kiwi = (rigs[name] for name in ('hf-east', 'hf-west', 'kiwi'))
        a = vfos[east]
        assert len({objects[uuid]['room'] for uuid in rigs.values()}) == len(rigs), 'bridge room collision'
        assert objects[east]['backend'] == 'bridge'
        assert states[east, 'online']['value'] is False
        assert states[west, 'online']['value'] is False
        assert not (work / 'east.pid').exists() and not (work / 'west.pid').exists(), 'idle bridges spawned'
        join(objects[east]['room'])
        state(east, 'online', True)
        join(objects[west]['room'])
        state(west, 'online', True)
        assert properties[a, 'frequency']['minimum'] == 100000
        assert properties[a, 'frequency']['maximum'] == 30000000
        assert properties[a, 'mode']['enum'] == 'USB AM CW'
        assert states[east, 'online']['value'] is True
        assert states[kiwi, 'online']['value'] is False, 'module loading must never mark Kiwi online'
        assert states[east, 'tx.supported']['value'] is False
        assert states[east, 'tx.available']['value'] is False
        join(objects[east]['room'])
        control(a, 'frequency', 7100000, 'tune')
        assert states[a, 'frequency']['value'] == 7100000
        control(a, 'mode', 'AM', 'mode')
        assert states[a, 'mode']['value'] == 'AM'
        control(a, 'frequency', 15000001, 'hardware-failure', 'backend-failure')
        assert states[a, 'frequency']['value'] == 7100000
        control(a, 'frequency', 99999, 'bad-range', 'invalid-value')
        control(a, 'mode', 'FM', 'bad-mode', 'invalid-value')
        pid = int((work / 'east.pid').read_text())
        os.kill(pid, signal.SIGKILL)
        state(east, 'online', False)
        state(a, 'frequency', available=False)
        assert states[west, 'online']['value'] is True
        state(east, 'online', True)
        state(a, 'frequency', 7100000, True)
        state(a, 'mode', 'AM', True)
        assert int((work / 'east.pid').read_text()) != pid
        assert vfos[east] == a
        # A restarted bridge remains controllable after desired-state replay.
        time.sleep(0.1)
        control(a, 'frequency', 7200000, 'after-restart')
        assert states[a, 'frequency']['value'] == 7200000
        # A wedged driver cannot leave a command pending or stop other rigs.
        wedged = int((work / 'east.pid').read_text())
        os.kill(wedged, signal.SIGSTOP)
        control(a, 'frequency', 7300000, 'wedged-command', 'backend-failure')
        assert states[east, 'online']['value'] is False
        assert states[west, 'online']['value'] is True
        state(east, 'online', True)
        state(a, 'frequency', 7200000, True)
        assert int((work / 'east.pid').read_text()) != wedged
        print('PASS: bridge discovery, descriptors, real IPC controls, rejected tuning, crash isolation and desired-state restore')
    except BaseException:
        print((work / 'console.log').read_text()[-12000:], file=sys.stderr)
        if (work / 'server.log').exists(): print((work / 'server.log').read_text()[-12000:], file=sys.stderr)
        raise
    finally:
        if client: client.socket.close()
        try:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
        finally:
            try: os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError: pass

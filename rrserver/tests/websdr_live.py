"""Temporary receiver creation, policy, demand, deduplication and saved configuration."""
import hashlib
import json
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
sys.path.insert(0, str(ROOT / 'rrbridge/tests'))
from kiwi_fixture import KiwiFixture
work = Path(sys.argv[1])
fixture = KiwiFixture()
second = KiwiFixture()
with sqlite3.connect(work / 'master.db') as db:
    db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
    for uid, name, privs in [(1, 'LISTENER', 'rx,chat,view,websdr.listen'),
                              (2, 'CONNECTOR', 'rx,chat,view,websdr.connect'),
                              (3, 'MANAGER', 'rx,chat,view,websdr.admin')]:
        db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(?,?,?,?,?,?)',
                   (uid, name, 1, hashlib.sha1(b'test-password').hexdigest(), 3, privs))
with socket.socket() as reservation:
    reservation.bind(('127.0.0.1', 0)); port = reservation.getsockname()[1]
config = work / 'server.cfg'
config.write_text(f'''[general]
station.name=websdrtest
rig.instances=
rig.default=configured
path.db.master={work}/master.db
log.file={work}/server.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port}
net.http.tls-enabled=false
net.mqtt.enabled=false
codecs.allowed=pc16
websdr.max-temporary=1
websdr.idle-timeout=2
[bridge]
instances=configured
path={ROOT}/bin/rrbridge
modules={ROOT}/bin
[bridge:configured]
driver=kiwi
host=127.0.0.1
port={fixture.port}
[fwdsp]
path={ROOT}/bin/fwdsp
subproc.max=24
pcm-hub=true
''' + TEST_ACLS)
env = dict(os.environ, LD_LIBRARY_PATH=str(ROOT))
clients = []

def login(name):
    client = WebSocket(port); clients.append(client)
    client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': name}})
    challenge = client.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
    password = hashlib.sha1((hashlib.sha1(b'test-password').hexdigest() + '+' + challenge['nonce']).encode()).hexdigest()
    client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': name, 'pass': password, 'token': challenge['token']}})
    client.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')
    client.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'initial'}})
    client.until(lambda m: m.get('object', {}).get('cmd') == 'end')
    return client

def request(client, endpoint, action='websdr-connect'):
    client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'admin', 'action': action,
        'data': json.dumps({'request': 'test', 'driver': 'kiwi', 'url': endpoint})}})
    result = client.until(lambda m: m.get('talk', {}).get('action') == 'websdr-result')
    return json.loads(result['talk']['data'])

def run():
    process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=work, env=env,
        stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
    for _ in range(200):
        if process.poll() is not None: raise RuntimeError('server failed: ' + (work / 'console.log').read_text())
        try:
            probe = socket.create_connection(('127.0.0.1', port), timeout=.1); probe.close(); return process
        except OSError: time.sleep(.025)
    raise RuntimeError('server did not start')

def stop(process):
    for client in clients: client.socket.close()
    clients.clear()
    if process.poll() is None: os.killpg(process.pid, signal.SIGTERM)
    try: process.wait(timeout=10)
    except subprocess.TimeoutExpired: os.killpg(process.pid, signal.SIGKILL); process.wait()

with (work / 'console.log').open('w') as console:
    process = run()
    try:
        listener = login('LISTENER'); connector = login('CONNECTOR'); manager = login('MANAGER')
        configured = f'http://127.0.0.1:{fixture.port}/'
        arbitrary = f'http://127.0.0.1:{second.port}/'
        assert not request(listener, configured)['error']
        assert 'websdr.connect' in request(listener, arbitrary)['error']
        assert 'websdr.admin' in request(connector, arbitrary, 'websdr-add')['error']
        assert request(connector, 'http://user@127.0.0.1:8073/')['error']
        assert not fixture.connections and not second.connections, 'discovery or rejected request contacted receiver'
        result = request(connector, arbitrary)
        assert not result['error'], result
        room = result['room']
        assert request(connector, arbitrary)['room'] == room, 'duplicate request created another rig'
        assert 'limit' in request(connector, 'http://other.invalid:8073/')['error']
        assert not second.connections, 'creating a rig connected before JOIN'
        listener.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': room}})
        listener.until(lambda m: 'websdr.connect' in str(m) and m.get('msg', {}).get('type') == 'error')
        connector.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': room}})
        connector.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == room)
        channel = connector.until(lambda m: m.get('media', {}).get('cmd') == 'available' and m['media'].get('room') == room)['media']
        connector.until(lambda m: m.get('property', {}).get('name') == 'online' and m['property'].get('value') is True)
        connector.send({'msg': {'type': 'media'}, 'media': {'cmd': 'subscribe', 'chan-uuid': channel['chan-uuid']}})
        connector.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed')
        connector.until_frame(lambda opcode, payload: opcode == 2 and payload)

        assert second.connections
        saved = request(manager, arbitrary, 'websdr-add')
        assert not saved['error'], saved
        assert 'websdr-' in config.read_text() and '[bridge:configured]' in config.read_text()
        assert not request(listener, arbitrary)['error'], 'saved receiver not available to websdr.listen'
        connector.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': room}})
        connector.until(lambda m: m.get('property', {}).get('name') == 'online' and m['property'].get('value') is False)
        idle = request(connector, 'http://idle.invalid:8073/')
        assert not idle['error'], idle
        time.sleep(2.2)
        assert not request(connector, 'http://next.invalid:8073/')['error'], 'idle temporary rig did not release its slot'
        stop(process)
        process = run()
        listener = login('LISTENER')
        assert not request(listener, arbitrary)['error'], 'saved config did not survive restart'
        print('PASS: configured/temporary/admin WebSDR permissions, no eager connection, endpoint validation, limits, deduplication, demand and config restart')
    except BaseException:
        print((work / 'console.log').read_text()[-12000:], file=sys.stderr)
        if (work / 'server.log').exists(): print((work / 'server.log').read_text()[-12000:], file=sys.stderr)
        raise
    finally:
        stop(process)
        fixture.stop(); second.stop()

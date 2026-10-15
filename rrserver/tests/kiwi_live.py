"""Real server, bridge processes, plugin loader, IPC and generic object wire path."""
import hashlib
import json
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
    db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(2,?,?,?,?,?)',
        ('VIEWER', 1, hashlib.sha1(b'test-password').hexdigest(), 3, 'view,chat,rx'))
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
record.always.vfo_a=true
[bridge]
instances=receiver
path={ROOT}/bin/rrbridge
modules={ROOT}/bin
rx-min-hz=500000
rx-max-hz=25000000
rx-modes=USB AM
[bridge:receiver]
driver=kiwi
host={kiwi_host}
port={kiwi_port}
rx-audio=true
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

def login():
    global client
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

with (work / 'console.log').open('w') as console:
    process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=work,
        env=env, stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        login()
        time.sleep(.3)
        client.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'kiwi-discovery'}})
        until(lambda m: m.get('object', {}).get('cmd') == 'end')
        rigs = {o['alias']: uuid for uuid, o in objects.items() if o['type'] == 'rig'}
        assert set(rigs) == {'rig0'}, rigs
        radio = rigs['rig0']
        a = next(uuid for uuid,o in objects.items() if o['type'] == 'vfo' and o['owner'] == radio)
        assert not states[radio, 'online'].get('value'), 'receiver connected without demand'
        assert states[radio, 'capabilities.source']['value'] == 'configured'
        assert properties[a, 'frequency']['minimum'] == 500000
        assert properties[a, 'frequency']['maximum'] == 25000000
        if fixture: assert fixture.connections == 0 and not fixture.commands, 'Kiwi contacted during startup or discovery'
        # Ordinary RX privilege alone cannot wake an external receiver.
        outsider = WebSocket(port)
        outsider.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': 'VIEWER'}})
        challenge = outsider.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
        password = hashlib.sha1((hashlib.sha1(b'test-password').hexdigest() + '+' + challenge['nonce']).encode()).hexdigest()
        outsider.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': 'VIEWER', 'pass': password, 'token': challenge['token']}})
        outsider.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')
        outsider.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': objects[radio]['room']}})
        denied = outsider.until(lambda m: 'websdr.listen' in str(m))
        assert denied
        outsider.socket.close()
        if fixture: assert fixture.connections == 0, 'unauthorized RX user started Kiwi'
        join(objects[radio]['room'])
        state(radio, 'online', True)
        assert states[radio, 'capabilities.source']['value'] == 'live'
        assert properties[a, 'frequency']['minimum'] == 0
        assert properties[a, 'frequency']['maximum'] == (28000000 if fixture else 30000000)
        with sqlite3.connect(work / 'master.db') as db:
            rows = db.execute("SELECT hints,updated_at FROM bridge_capabilities WHERE identity NOT LIKE '%.metadata.%'").fetchall()
            assert len(rows) == 1 and rows[0][1] > 0
            assert json.loads(rows[0][0])['rx-max-hz'] == ('28000000' if fixture else '30000000')
        if fixture:
            assert states[radio, 'receiver.contact']['value'] == 'fixture@example.invalid'
            assert states[radio, 'receiver.name']['value'] == 'Fixture Kiwi'
            with sqlite3.connect(work / 'master.db') as db:
                assert 'must-not-cache' not in repr(db.execute('SELECT hints FROM bridge_capabilities').fetchall())
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
            fixture.confirm_delay = .7
            client.send({'msg': {'type': 'property'}, 'target': a,
                'property': {'cmd': 'set', 'name': 'frequency', 'type': 'long', 'value': 7200000},
                'request': {'id': 'delayed-tune'}})
            audio_times = []
            def pending_result(opcode, payload):
                if opcode == 2 and payload[:8] == b'RR\x01\x01pc16':
                    audio_times.append(time.monotonic())
                if opcode != 1: return False
                message = track(json.loads(payload))
                return message.get('property', {}).get('cmd') == 'result' and message.get('request', {}).get('id') == 'delayed-tune'
            _, result = client.until_frame(pending_result)
            assert result['result']['code'] == 'ok', result
            assert len(audio_times) >= 4 and audio_times[-1] - audio_times[0] >= .3, 'audio withheld while awaiting tuning confirmation'
            fixture.confirm_delay = 0
        # Exercise tuning while media is already subscribed, rather than only
        # setting up the receiver before the audio path exists.
        for i in range(12):
            control(a, 'frequency', 7100000 + (i % 3) * 1000, f'kiwi-retune-{i}')
            control(a, 'mode', ('USB', 'LSB', 'CW', 'AM')[i % 4], f'kiwi-remode-{i}')
            _, frame = client.until_frame(lambda opcode,data: opcode == 2 and data[:8] == b'RR\x01\x01pc16')
            assert any(frame[28:]), f'audio stopped after tuning cycle {i}'
        # Native clients can still send legacy CAT commands for bridge rooms.
        for i in range(4):
            frequency = 7300000 + i * 1000
            client.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': objects[radio]['room'],
                'vfo': 'A', 'freq': frequency}})
            state(a, 'frequency', frequency)
            mode = ('USB', 'LSB', 'CW', 'AM')[i]
            client.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'mode', 'room': objects[radio]['room'],
                'vfo': 'A', 'mode': mode}})
            state(a, 'mode', mode)
            _, frame = client.until_frame(lambda opcode,data: opcode == 2 and data[:8] == b'RR\x01\x01pc16')
            assert any(frame[28:]), f'audio stopped after CAT cycle {i}'
        control(a, 'frequency', 7100000, 'kiwi-restore-frequency')
        control(a, 'mode', 'AM', 'kiwi-restore-mode')
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
        if fixture:
            client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': objects[radio]['room']}})
            until(lambda m: m.get('talk', {}).get('cmd') == 'part')
            state(radio, 'online', False)
            time.sleep(.4)
            assert fixture.peer is None, 'last user leaving did not release Kiwi'
            commands = len(fixture.commands)
            connections = fixture.connections
            time.sleep(1.2)
            assert fixture.peer is None and fixture.connections == connections and len(fixture.commands) == commands, 'idle receiver retried'
            join(objects[radio]['room'])
            state(radio, 'online', True)
            assert states[a, 'frequency']['value'] == 7100000
            assert states[a, 'mode']['value'] == 'AM'
        console_text = (work / 'console.log').read_text()
        assert 'Unable to apply' not in (work / 'server.log').read_text(), 'pending CAT request logged as a failure'
        if fixture:
            assert 'retrying in 2 seconds' in console_text, 'brief reception reset reconnect backoff'
            assert 'Invalid or unsupported Kiwi stream' not in console_text, 'duplicate parser error after authentication rejection'
        bridge_logs = [line for line in (work / 'console.log').read_text().splitlines() if 'Kiwi receiver online' in line]
        assert bridge_logs and all(re.match(r'\[20\d\d/\d\d/\d\d \d\d:\d\d:\d\d\] <rrbridge@info>', line) for line in bridge_logs), bridge_logs
        if fixture:
            client.socket.close(); client = None
            os.killpg(process.pid, signal.SIGTERM)
            process.wait(timeout=5)
            time.sleep(.3)
            command_count = len(fixture.commands)
            connection_count = fixture.connections
            # No configured hints on the second boot: only live-learned DB metadata.
            config.write_text(re.sub(r'^rx-[^\n]*\n', '', config.read_text(), flags=re.MULTILINE))
            objects.clear(); properties.clear(); states.clear()
            process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=work,
                env=env, stdout=console, stderr=subprocess.STDOUT, start_new_session=True)
            login()
            client.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'cached-discovery'}})
            until(lambda m: m.get('object', {}).get('cmd') == 'end')
            assert states[radio, 'capabilities.source']['value'] == 'cached'
            assert not states[radio, 'online']['value']
            assert properties[a, 'frequency']['maximum'] == 28000000
            assert states[radio, 'receiver.contact']['value'] == 'fixture@example.invalid'
            assert states[radio, 'receiver.name']['value'] == 'Fixture Kiwi'
            assert fixture.peer is None and fixture.connections == connection_count and len(fixture.commands) == command_count, 'cached discovery contacted Kiwi'
        print('PASS: on-demand Kiwi reception, tuning/audio, idle release and live coverage cached across server restart')
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

"""Run real rrservers: mutual WSS auth, bidirectional dials, tree and reconnect."""
import base64
import hashlib
import json
import os
import pathlib
import re
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import time
from ws_helpers import TEST_ACLS, WebSocket
ROOT = pathlib.Path(__file__).resolve().parents[2]
LISTENER = os.environ.get('RR_LINK_LISTENER', 'dedicated')
PLAINTEXT = LISTENER == 'shared-plain'
DIRECT_MEDIA = bool(os.environ.get('RR_RTP_TEST_PEER'))
PROXY_MEDIA = bool(os.environ.get('RR_PROXY_TEST'))

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

class LinkSocket(WebSocket):
    def __init__(self, port, context, protocol='rustyrig-link.v1'):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=3)
        if context: self.socket = context.wrap_socket(self.socket, server_hostname='localhost')
        self.buffer = b''
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f'GET /link HTTP/1.1\r\nHost: localhost:{port}\r\n'
            f'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
            f'Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: {protocol}\r\n\r\n').encode())
        while b'\r\n\r\n' not in self.buffer:
            data = self.socket.recv(4096)
            if not data: raise EOFError()
            self.buffer += data
        self.headers, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
    def send(self, data):
        self.send_payload(json.dumps(data).encode(), 1)
    def receive(self):
        first, size = self.exact(2)
        opcode = first & 15
        size &= 127
        if size == 126: size = int.from_bytes(self.exact(2), 'big')
        elif size == 127: size = int.from_bytes(self.exact(8), 'big')
        data = self.exact(size)
        if opcode == 8: raise EOFError()
        return json.loads(data) if opcode == 1 else None

with tempfile.TemporaryDirectory(prefix='rr-links-') as temporary:
    work = pathlib.Path(temporary)
    cert, key = work / 'cert.pem', work / 'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
        '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    context = None if PLAINTEXT else ssl.create_default_context(cafile=str(cert))
    ports = {name: port() for name in ('alpha', 'beta', 'gamma', 'delta')}
    processes, outputs, client_ports = {}, {}, {}
    media_ports = {name: port() for name in ports}
    reservations = []
    if DIRECT_MEDIA:
        for name in media_ports:
            while True:
                start_port = port()
                if start_port > 65528: continue
                held = []
                try:
                    for udp_port in range(start_port, start_port + 8):
                        reserved = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        held.append(reserved); reserved.bind(('127.0.0.1', udp_port))
                except OSError:
                    for reserved in held: reserved.close()
                    continue
                held[-1].close()
                reservations.extend(held[:-1])
                media_ports[name] = start_port
                break
    def peer(name, remote, url=True):
        return f'''[{'peer' if name == 'alpha' and remote == 'beta' else 'peer:' + remote}]
enabled=true
autoconnect=true
server.url={('ws://' if PLAINTEXT else 'wss://') + 'localhost:' + str(ports[remote]) + '/link' if url else ''}
password=disposable-link-secret
site.name={remote}
acl=test-local
retry-seconds={2 if name == "alpha" and remote == "beta" else 1}
'''
    def start(name, ca=None, deny_peers=False, direct_disabled=False):
        database = work / f'{name}.db'
        if not database.exists():
            with sqlite3.connect(database) as db:
                db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
                db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,'TEST',1,?,3,'admin,view,chat')",
                    (hashlib.sha1(b'test-password').hexdigest(),))
                if name == 'alpha':
                    # Saved rooms from an old site identity must yield to its owner.
                    db.executemany("INSERT INTO rooms(name) VALUES(?)", [('#GAMMA',), ('#gamma-rig0',), ('#ordinary-local',)])
                    db.execute("INSERT INTO chat_log(msg_src,msg_dest,msg_type,msg_data) VALUES('TEST','#GAMMA','pub','retained-history')")
                    db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(2,'MISSING',1,?,3,'admin,view,chat')", (hashlib.sha1(b'test-password').hexdigest(),))
                db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(3,'READER',1,?,3,?)", (hashlib.sha1(b'test-password').hexdigest(), 'admin,view,chat' if name == 'alpha' else 'view,chat'))
        if DIRECT_MEDIA or PROXY_MEDIA:
            with sqlite3.connect(database) as db:
                db.execute("INSERT OR IGNORE INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(4,'SOURCE',1,?,3,'media.source,view,chat')", (hashlib.sha1(b'test-password').hexdigest(),))
        client_ports[name] = ports[name] if PLAINTEXT else port()
        config = work / f'{name}.cfg'
        settings = f'''[general]
station.name={name}
rig.instances=rig0
rig.default=rig0
path.db.master={database}
path.modules={work}/modules
log.file={work}/{name}.log
log.level=*:info,relay:debug
net.http.bind=127.0.0.1
net.http.port={client_ports[name]}
net.http.tls-enabled=false
net.http.authdb-dynamic=true
net.http.www-root={ROOT}/www
net.http.ua-bans={work}/absent
net.mqtt.enabled=false
net.mqtt-client.enabled=false
links.listen=https://127.0.0.1:{ports[name]}
links.tls-cert={cert}
links.tls-key={key}
links.tls-ca={ca or cert}
links.retry-seconds=1
links.handshake-seconds=3
atu.max=0
quota.enforce=false
chat.replay-lines=0
media.direct.enabled=true
media.direct.host={'127.0.0.1' if DIRECT_MEDIA else ''}
media.direct.bind=127.0.0.1
media.direct.port={media_ports[name]}
codecs.allowed=pc16
[rig:rig0]
backend=internal
vfos={'A,B' if name == 'gamma' else 'A'}
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
'''
        if DIRECT_MEDIA:
            settings += f"""[rtp]
enabled=false
[acl:media-staff]
deny=192.0.2.0/24
allow=@admin@127.0.0.0/8
[acl:media-denied]
allow=192.0.2.0/24
deny=all
[rtp:denied]
enabled=true
host=127.0.0.1
bind=127.0.0.1
ports={port()}
acl=media-denied
[rtp:staff]
enabled=true
host=127.0.0.1
bind=127.0.0.1
ports={media_ports[name]}-{media_ports[name] + 7}
acl=media-staff
"""
        if LISTENER != 'dedicated':
            settings = settings.replace(f'links.listen=https://127.0.0.1:{ports[name]}', 'links.listen=')
            settings = settings.replace('net.http.tls-enabled=false',
                f'net.http.tls-enabled={"false" if PLAINTEXT else "true"}\n'
                f'net.http.tls-bind=127.0.0.1\nnet.http.tls-port={ports[name]}\n'
                f'net.http.tls-cert={cert}\nnet.http.tls-key={key}\n'
                f'links.allow-insecure={"true" if PLAINTEXT else "false"}')
        if name == 'alpha': settings += peer(name, 'beta') + peer(name, 'delta', False)
        elif name == 'beta': settings += peer(name, 'alpha') + peer(name, 'gamma', False)
        elif name == 'gamma': settings += peer(name, 'beta') + peer(name, 'delta', False)
        else: settings += peer(name, 'gamma')
        # Explicitly disabled incomplete sections must be harmless.
        settings += '[peer:disabled]\nenabled=false\nautoconnect=true\n'
        if deny_peers:
            settings = settings.replace('acl=test-local', 'acl=test-deny') + '[acl:test-deny]\ndeny=all\n'
        if direct_disabled:
            settings = settings.replace('media.direct.enabled=true', 'media.direct.enabled=false')
        config.write_text(settings + TEST_ACLS)
        outputs[name] = open(work / f'{name}.console', 'a')
        env = dict(os.environ, LD_LIBRARY_PATH=str(ROOT))
        processes[name] = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=ROOT,
            stdout=outputs[name], stderr=subprocess.STDOUT, env=env)
    def log(name):
        return (work / f'{name}.console').read_text()
    def wait(predicate, label, timeout=20):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            for name, p in processes.items():
                if p.poll() is not None: raise AssertionError(f'{name} exited:\n{log(name)}')
            if predicate(): return
            time.sleep(.1)
        raise AssertionError(label + '\n' + '\n'.join(f'{n}:\n{log(n)}' for n in processes))
    def stop(name):
        p = processes.pop(name)
        p.terminate()
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill(); p.wait()
        outputs.pop(name).close()
    try:
        start('alpha')
        def accepts():
            try:
                probe = LinkSocket(ports['alpha'], context, 'rustyrig.v1')
                assert b'400' in probe.headers
                probe.socket.close()
                return True
            except (ConnectionRefusedError, ssl.SSLError): return False
        wait(accepts, 'link listener did not start')
        if LISTENER == 'shared':
            denied = LinkSocket(client_ports['alpha'], None)
            assert b'403' in denied.headers
            denied.socket.close()
            print('PASS: shared plaintext listener rejects links unless explicitly enabled')
        # Invalid credentials must close without replying with local credentials.
        bad = LinkSocket(ports['alpha'], context)
        assert b'101' in bad.headers
        bad.send({'op':'link.hello', 'node':'00000000-0000-4000-8000-000000000002',
            'site':'beta', 'password':'wrong'})
        try:
            bad.receive()
            raise AssertionError('unauthorized peer received a response')
        except (EOFError, ConnectionResetError): pass
        finally: bad.socket.close()
        wrong_site = LinkSocket(ports['alpha'], context)
        wrong_site.send({'op':'link.hello', 'node':'00000000-0000-4000-8000-000000000002',
            'site':'unexpected-site',
            'password':'disposable-link-secret'})
        try:
            wrong_site.receive()
            raise AssertionError('unexpected site was accepted')
        except (EOFError, ConnectionResetError): pass
        finally: wrong_site.socket.close()
        start('beta', deny_peers=True)
        wait(lambda: ': Outbound peer ACL access denied' in log('beta') and ': ACL access denied' in log('beta'), 'peer deny ACL was not enforced')
        assert 'Linked site' not in log('beta') and 'Linked site beta' not in log('alpha')
        stop('beta')
        print('PASS: peer ACL denies incoming and outbound admission before linking')
        start('beta')
        wait(lambda: 'Linked site beta' in log('alpha') and 'Linked site alpha' in log('beta'), 'mutual two-server link failed')
        # A late authenticated duplicate must preserve the established link.
        node = re.search(r'Server link node ([0-9a-f-]+)', log('beta')).group(1)
        before_duplicates = log('alpha').count('Unlinked site beta')
        duplicate = LinkSocket(ports['alpha'], context)
        duplicate.send({'op':'link.hello', 'node':node, 'site':'beta',
            'password':'disposable-link-secret'})
        try:
            duplicate.receive()
            raise AssertionError('late duplicate was accepted')
        except (EOFError, ConnectionResetError): pass
        finally: duplicate.socket.close()
        assert log('alpha').count('Unlinked site beta') == before_duplicates
        if not PLAINTEXT:
            # Native outbound TLS must reject an unrelated trust anchor.
            bad_cert, bad_key = work / 'bad-ca.pem', work / 'bad-key.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                '-subj', '/CN=untrusted', '-keyout', str(bad_key), '-out', str(bad_cert)],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            start('gamma', bad_cert)
            wait(lambda: 'TLS handshake:' in log('gamma'), 'outbound TLS did not reject untrusted certificate')
            assert 'Linked site beta' not in log('gamma')
            stop('gamma')
        start('delta')
        start('gamma')
        wait(lambda: 'Introduced server' in log('alpha') and 'servers 4' in log('alpha') and
            'Linked site beta' in log('gamma') and 'servers 4' in log('gamma') and 'Linked site gamma' in log('delta'), 'four-server tree did not converge')
        # A configured peer already in our component must not create a cycle.
        cycle = LinkSocket(ports['alpha'], context)
        delta_node = re.search(r'Server link node ([0-9a-f-]+)', log('delta')).group(1)
        cycle.send({'op':'link.hello', 'node':delta_node, 'site':'delta',
            'password':'disposable-link-secret'})
        hello = cycle.receive()
        assert hello['op'] == 'link.hello' and 'username' not in hello and 'role' not in hello and 'root' not in hello
        assert cycle.receive()['op'] == 'link.snapshot'
        cycle.send({'op':'link.snapshot', 'nodes':f"{delta_node},- {hello['node']},{delta_node}"})
        try:
            cycle.receive()
            raise AssertionError('cyclic link admitted')
        except (EOFError, ConnectionResetError): pass
        finally: cycle.socket.close()
        wait(lambda: 'Topology overlap' in log('alpha'), 'cycle was not rejected by topology admission')
        assert 'Unlinked site' not in log('delta')
        print('PASS: symmetric peer handshake and live cycle rejection preserve the existing tree')
        def login(name, username='TEST'):
            c = WebSocket(ports[name], context) if LISTENER == "shared" and name == "gamma" else WebSocket(client_ports[name])
            c.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': username}})
            challenge = c.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
            first_hash = hashlib.sha1(b'test-password').hexdigest()
            password = hashlib.sha1((first_hash + '+' + challenge['nonce']).encode()).hexdigest()
            c.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': username,
                'pass': password, 'token': challenge['token']}})
            c.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')
            return c
        origin, owner = login('alpha'), login('gamma')
        # Admin listings come from SQLite, including offline/locked accounts.
        with sqlite3.connect(work / 'alpha.db') as db:
            db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(90,'OFFLINE',0,'secret-hash',1,'view')")
            db.executemany("INSERT INTO audit_log(username,event_type,details) VALUES('TEST','fixture',?)", [(f'entry-{i}',) for i in range(105)])
            db.execute("INSERT INTO ptt_log(username,vfo,frequency,mode,bandwidth,power,recording_id,stop_reason) VALUES('TEST','A',7200000,'LSB',2700,1,'recording-id','timeout')")
        def admin_page(table, before=None):
            origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'admin', 'data': table + (' ' + str(before) if before else '')}})
            rows, metadata = [], {}
            def complete(message):
                if message.get('talk', {}).get('cmd') != 'admin': return False
                reply = message['talk']
                payload = json.loads(reply['data'])
                if reply['action'] == 'row': rows.append(payload)
                if reply['action'] == 'end': metadata.update(payload); return True
                return False
            origin.until(complete)
            return rows, metadata
        users, _ = admin_page('users')
        assert any(row['name'] == 'OFFLINE' and str(row['enabled']) == '0' for row in users), users
        assert all('password' not in row and 'password_set' in row for row in users)
        rooms, _ = admin_page('rooms')
        assert any(row['name'] == '#ordinary-local' for row in rooms)
        audit, page = admin_page('audit')
        assert len(audit) == 100 and int(page['next']) > 0
        older, _ = admin_page('audit', page['next'])
        assert older and not {row['id'] for row in audit}.intersection(row['id'] for row in older)
        ptt, _ = admin_page('ptt')
        assert any(row['recording_id'] == 'recording-id' and row['stop_reason'] == 'timeout' for row in ptt)
        denied = login('gamma', 'READER')
        denied.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'admin', 'data': 'users'}})
        denied.until(lambda message: message.get('msg', {}).get('type') == 'error')
        denied.socket.close()
        print('PASS: SQLite admin inventory, locked/offline accounts, credential exclusion, audit paging, PTT recording IDs and permission gate')

        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'list'}})
        listed = origin.until(lambda m: m.get('talk', {}).get('cmd') == 'room-list')
        assert '#gamma-rig0' in listed['talk']['rooms'], listed
        assert '#gamma-rig0' in listed['talk']['remote-rooms'], listed
        assert '#gamma' in listed['talk']['remote-rooms'], listed
        assert '#GAMMA' not in listed['talk']['rooms'].split(), listed
        with sqlite3.connect(work / 'alpha.db') as db:
            assert db.execute("SELECT count(*) FROM rooms WHERE name COLLATE NOCASE IN ('#gamma','#gamma-rig0')").fetchone()[0] == 0
            assert db.execute("SELECT count(*) FROM rooms WHERE name IN ('#alpha','#alpha-rig0','#ordinary-local') AND deleted=0").fetchone()[0] == 3
            assert db.execute("SELECT count(*) FROM chat_log WHERE msg_data='retained-history'").fetchone()[0] == 1
            assert db.execute("SELECT count(*) FROM audit_log WHERE username='server-link' AND event_type='room.removed'").fetchone()[0] == 2
        print('PASS: authoritative linked rooms replace stale local metadata without deleting history or current local rooms')
        assert '#alpha-rig0' not in listed['talk']['remote-rooms'], listed
        assert '↗gamma' in listed['talk']['servers'], listed
        def remote_users_visible():
            rows = []
            origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'users', 'target': '#alpha'}})
            def finished(m):
                text = m.get('talk', {}).get('data', '')
                rows.append(text)
                return text in ('End of users', 'No users online')
            origin.until(finished)
            return any('TEST@gamma' in row for row in rows)
        wait(remote_users_visible, '/users did not discover remote online user')
        missing = login('alpha', 'MISSING')
        missing.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': '#gamma-rig0'}})
        def missing_error(m):
            return 'enabled local account' in m.get('error', {}).get('msg', '')
        missing.until(missing_error)
        missing.socket.close()
        reader = login('alpha', 'READER')
        reader.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': '#gamma-rig0'}})
        reader.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == '#gamma-rig0')
        reader.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'A', 'freq': 14300000}})
        reader.until(lambda m: bool(m.get('error', {}).get('msg')))
        # Two local subscribers share one upstream branch subscription.
        second = login('alpha')
        second.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': '#gamma-rig0'}})
        second.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == '#gamma-rig0')
        origin.send({'msg': {'type': 'object'}, 'object': {'cmd': 'snapshot'}, 'request': {'id': 'relay-objects'}})
        objects = []
        def snapshot_done(m):
            if m.get('object', {}).get('cmd') == 'descriptor': objects.append(m['object'])
            return m.get('object', {}).get('cmd') == 'end' and m.get('request', {}).get('id') == 'relay-objects'
        origin.until(snapshot_done)
        assert any(o.get('type') == 'vfo' and o.get('room') == '#gamma-rig0' for o in objects), objects
        for c in (origin, owner):
            c.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': '#gamma-rig0'}})
            c.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == '#gamma-rig0')
        # Control is applied at gamma and observed at both ends of a two-hop path.
        sent_before = log('gamma').count('State cat freq room #gamma-rig0')
        origin.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'A', 'freq': 14250000}})
        for c in (owner, origin, second):
            c.until(lambda m: m.get('cat', {}).get('freq') == 14250000 and m['cat'].get('room') == '#gamma-rig0')
        assert log('gamma').count('State cat freq room #gamma-rig0') == sent_before + 1
        owner.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'msg', 'msg_type': 'pub', 'target': '#gamma-rig0', 'data': 'owner-to-tree'}})
        origin.until(lambda m: m.get('talk', {}).get('data') == 'owner-to-tree')
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'msg', 'msg_type': 'pub', 'target': '#gamma-rig0', 'data': 'tree-to-owner'}})
        owner.until(lambda m: m.get('talk', {}).get('data') == 'tree-to-owner')
        origin.until(lambda m: m.get('talk', {}).get('data') == 'tree-to-owner')
        for command, argument in [('mode', 'LSB'), ('width', '1800'), ('power', '10')]:
            origin.send({'msg': {'type': 'cat'}, 'cat': {'cmd': command, 'room': '#gamma-rig0', 'vfo': 'A', command: argument}})
            for c in (owner, origin):
                c.until(lambda m: m.get('cat', {}).get('cmd') == command and m['cat'].get('room') == '#gamma-rig0')
        # Owner property controls use the same object identity found in discovery.
        vfo = next(o['uuid'] for o in objects if o.get('type') == 'vfo' and o.get('room') == '#gamma-rig0' and o.get('alias') == 'A')
        origin.send({'msg': {'type': 'property'}, 'property': {'cmd': 'set', 'name': 'frequency', 'value': 14260000},
            'target': vfo, 'request': {'id': 'remote-frequency', 'room': '#gamma-rig0'}})
        changed = []
        def property_applied(m):
            if m.get('property', {}).get('cmd') == 'changed' and m.get('target') == vfo and m['property'].get('value') == 14260000:
                changed.append(m)
            return m.get('property', {}).get('cmd') == 'result' and m.get('result', {}).get('code') == 'ok'
        origin.until(property_applied)
        if not changed:
            origin.until(lambda m: m.get('property', {}).get('cmd') == 'changed' and m.get('target') == vfo and m['property'].get('value') == 14260000)
        # All subscribers leave: subsequent CAT state must not traverse beta.
        for c in (origin, second, reader):
            c.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': '#gamma-rig0'}})
            c.until(lambda m: m.get('talk', {}).get('cmd') == 'part' and m['talk'].get('room') == '#gamma-rig0' and 'session' in m['talk'])
        wait(lambda: 'Subscription removed' in log('gamma'), 'final room unsubscribe did not reach owner')
        idle_before = log('gamma').count('State cat freq room #gamma-rig0')
        owner.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'A', 'freq': 14270000}})
        owner.until(lambda m: m.get('cat', {}).get('freq') == 14270000)
        assert log('gamma').count('State cat freq room #gamma-rig0') == idle_before
        # RX-only rooms subscribe only to their mapped VFOs, while sharing rig ownership.
        monitor = '#gamma-rig0.monitor'
        owner.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'room', 'data': monitor + ' add'}})
        owner.until(lambda m: monitor + ' added' in m.get('notice', {}).get('msg', ''))
        owner.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'room', 'data': monitor + ' vfo add rig0.vfo_b'}})
        owner.until(lambda m: monitor + ' VFO ' in m.get('notice', {}).get('msg', ''))
        def monitor_visible():
            origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'list'}})
            m = origin.until(lambda m: m.get('talk', {}).get('cmd') == 'room-list')
            return monitor in m['talk']['rooms']
        wait(monitor_visible, 'new remote RX room was not advertised')
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': monitor}})
        origin.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == monitor and 'session' in m['talk'])
        # A request/reply round trip to the owner also orders the subscription.
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'topic', 'target': monitor, 'data': ''}})
        origin.until(lambda m: m.get('talk', {}).get('cmd') == 'topic' and m['talk'].get('room') == monitor)
        unmapped_before = log('gamma').count('State cat freq room #gamma-rig0')
        owner.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'A', 'freq': 14275000}})
        owner.until(lambda m: m.get('cat', {}).get('freq') == 14275000)
        assert log('gamma').count('State cat freq room #gamma-rig0') == unmapped_before
        owner.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'B', 'freq': 14290000}})
        origin.until(lambda m: m.get('cat', {}).get('freq') == 14290000 and m['cat'].get('vfo') == 'B')
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'part', 'target': monitor}})
        origin.until(lambda m: m.get('talk', {}).get('cmd') == 'part' and m['talk'].get('room') == monitor and 'session' in m['talk'])
        print('PASS: dynamic remote room discovery and mapped-VFO-only RX subscriptions')
        for c in (second, reader): c.socket.close()
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'join', 'target': '#gamma-rig0'}})
        origin.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == '#gamma-rig0' and 'session' in m['talk'])
        print('PASS: multi-hop discovery and /users, remote LIST markers, owner permissions, chat/freq/mode/width/power, shared subscriptions and final unsubscribe')
        if DIRECT_MEDIA:
            from media_direct_live import run
            run(login, origin, owner, work, media_ports['gamma'] + 7, media_ports['alpha'] + 7)
        before = log('alpha').count('Linked site beta')
        leaf_links = log('gamma').count('Unlinked site delta')
        stop('beta')
        wait(lambda: 'Unlinked site beta, servers 1' in log('alpha') and 'Unlinked site beta, servers 2' in log('gamma'), 'disconnect did not withdraw only the lost branch')
        offline = origin.until(lambda m: m.get('talk', {}).get('cmd') == 'room-state' and m['talk'].get('room') == '#gamma-rig0')
        assert offline['talk']['connected'] is False
        assert log('gamma').count('Unlinked site delta') == leaf_links
        assert 'Unlinked site gamma' not in log('delta')
        start('beta')
        wait(lambda: log('alpha').count('Linked site beta') > before and
            log('alpha').count('servers 4') >= 2, 'autoconnect did not restore tree')
        origin.until(lambda m: m.get('talk', {}).get('cmd') == 'join' and m['talk'].get('room') == '#gamma-rig0' and 'session' in m['talk'])
        origin.send({'msg': {'type': 'cat'}, 'cat': {'cmd': 'freq', 'room': '#gamma-rig0', 'vfo': 'A', 'freq': 14280000}})
        for c in (owner, origin): c.until(lambda m: m.get('cat', {}).get('freq') == 14280000)
        origin.socket.close(); owner.socket.close()
        print('PASS: fresh authorized room subscription and control restored after middle-server restart')
        if DIRECT_MEDIA or PROXY_MEDIA:
            # Proxied TX must work even when the rig owner has no RTP listener.
            previous = log('gamma').count('Linked site beta')
            stop('gamma'); start('gamma', direct_disabled=True)
            wait(lambda: log('gamma').count('Linked site beta') > previous and
                 log('gamma').count('servers 4') >= 2, 'owner without RTP did not rejoin the tree')
            from media_direct_live import run_proxy
            run_proxy(login, work, media_ports['alpha'] + 7 if DIRECT_MEDIA else None)
        assert 'disposable-link-secret' not in ''.join(log(n) for n in processes)
        origin = login("alpha")
        # Explicit close suppresses outbound retries and incoming redials.
        baseline = log('alpha').count('Linked site beta')
        departures = log('alpha').count('Unlinked site beta')
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link', 'data': 'close peer'}})
        origin.until(lambda message: 'close requested' in message.get('notice', {}).get('msg', ''))
        wait(lambda: log('alpha').count('Unlinked site beta') > departures, 'manual peer close failed')
        time.sleep(3)
        assert log('alpha').count('Linked site beta') == baseline
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link', 'data': 'list'}})
        status = origin.until(lambda message: 'retry=2s' in message.get('notice', {}).get('msg', ''))
        assert 'closed' in status['notice']['msg']
        origin.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link', 'data': 'open peer'}})
        origin.until(lambda message: 'open requested' in message.get('notice', {}).get('msg', ''))
        wait(lambda: log('alpha').count('Linked site beta') > baseline, 'manual peer open failed')
        print('PASS: per-peer interval override, stored-peer close suppresses redials, open restores link')
        print(f'PASS: {LISTENER} listener, peer authentication, separate protocol, concurrent dials, incoming-only peer, branch-only withdrawal, component merging and autoconnect')
    except Exception:
        for name in processes:
            print(name, log(name), flush=True)
        raise
    finally:
        for name in list(processes): stop(name)
        for reserved in reservations: reserved.close()

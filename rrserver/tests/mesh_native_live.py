"""Real enrollment, pinned mutual WSS, topology, client continuity and recovery.

All keys, configurations, databases and sockets are disposable and independent.
No peer sections/passwords or global installed identity directories are used.
"""
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
import uuid

from ws_helpers import TEST_ACLS, WebSocket

ROOT = pathlib.Path(__file__).resolve().parents[2]


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Fixture:
    def __init__(self, work, name):
        self.name = name
        self.work = work / name
        self.work.mkdir()
        (self.work / 'modules').mkdir()
        self.node = str(uuid.uuid4())
        self.http, self.mesh = (4422, 4420) if name == 'alpha' and os.getenv('RR_MESH_FIXED_PORTS') else (port(), port())
        self.db = self.work / 'master.db'
        self.cert, self.key = self.work / 'cert.pem', self.work / 'key.pem'
        self.config = self.work / 'server.cfg'
        self.console = self.work / 'console.log'
        self.proc = None
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-subj', f'/CN={name}', '-keyout', str(self.key), '-out', str(self.cert)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.key.chmod(0o600)
        with sqlite3.connect(self.db) as db:
            db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
            db.execute('INSERT INTO rig_identities(identity_namespace,alias,uuid) VALUES(?,?,?)',
                       ('@server', '@node', self.node))
            db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,'TEST',1,?,20,'owner,admin,view,chat')",
                       (hashlib.sha1(b'test-password').hexdigest(),))
        self.config.write_text(f'''[general]
domain=mesh.test
station.name={name}
station.gps.position=42,-71
rig.instances=rig0
rig.default=rig0
path.db.master={self.db}
path.identity={self.work}/identity
path.modules={self.work}/modules
log.file={self.work}/server.log
log.level=*:info,mesh.native:debug
net.http.bind=127.0.0.1
net.http.tls-bind=127.0.0.1
net.http.tls-port={4420 if name == 'alpha' and os.getenv('RR_MESH_FIXED_PORTS') else self.http}
net.http.tls-enabled=true
net.http.tls-cert={self.cert}
net.http.tls-key={self.key}
net.http.authdb-dynamic=true
net.http.ua-bans={self.work}/absent
net.http.www-root={self.work}
links.listen=
links.native.enabled=true
links.native.bind=127.0.0.1
links.native.port={self.mesh}
links.retry-seconds=1
links.handshake-seconds=3
links.startup-delay-seconds=0
atu.max=0
quota.enforce=false
chat.replay-lines=0
media.direct.enabled=false
codecs.allowed=pc16
serial.ttyNETWORK=serial:/dev/null@115200,8n1
[rig:rig0]
backend=internal
vfos=A
[fwdsp]
path={self.work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
''' + TEST_ACLS)
        self.context = ssl.create_default_context(cafile=str(self.cert))
        self.context.check_hostname = False  # Fixture CA is exactly this server's cert.

    def log(self):
        return self.console.read_text() if self.console.exists() else ''

    def start(self):
        self.output = self.console.open('a')
        self.proc = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(self.config)], cwd=ROOT,
            stdout=self.output, stderr=subprocess.STDOUT, env=dict(os.environ, LD_LIBRARY_PATH=str(ROOT)))
        wait(lambda: self.accepts(), f'{self.name} listener', [self])

    def accepts(self):
        try:
            with socket.create_connection(('127.0.0.1', self.http), timeout=.2):
                return True
        except OSError:
            return False

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
                raise AssertionError('server failed to shut down')
            finally:
                self.proc = None
                self.output.close()

    def login(self, native=False):
        client = WebSocket(self.mesh if native else self.http, self.context)
        client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': 'TEST'}})
        challenge = client.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
        secret = hashlib.sha1(b'test-password').hexdigest()
        password = hashlib.sha1((secret + '+' + challenge['nonce']).encode()).hexdigest()
        client.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': 'TEST',
            'pass': password, 'token': challenge['token']}})
        client.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')
        return client


def wait(predicate, label, servers, timeout=20):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        for server in servers:
            if server.proc and server.proc.poll() is not None:
                raise AssertionError(f'{server.name} exited: {server.log()[-12000:]}')
        if predicate():
            return
        time.sleep(.05)
    raise AssertionError(label + '\n' + '\n'.join(s.log()[-10000:] for s in servers))


def notice_text(message):
    return message.get('notice', {}).get('msg', '') or message.get('talk', {}).get('data', '')


def enroll(issuer, joining):
    client = issuer.login(native=True)
    client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link',
        'data': f'invite {joining.node} 127.0.0.1:{issuer.mesh}'}})
    reply = client.until(lambda m: 'enrollment token' in notice_text(m))
    token = notice_text(reply).split(': ', 1)[1]
    client.socket.close()
    token_file = joining.work / 'invitation'
    token_file.write_text(token)
    token_file.chmod(0o600)
    result = subprocess.run([str(ROOT / 'bin/rrserver'), '-f', str(joining.config),
        '--mesh-join', str(token_file)], cwd=ROOT, env=dict(os.environ, LD_LIBRARY_PATH=str(ROOT)),
        text=True, capture_output=True, timeout=25)
    token_file.unlink()
    assert result.returncode == 0, result.stdout + result.stderr
    with sqlite3.connect(joining.db) as db:
        endpoint = db.execute('SELECT url FROM mesh_link_endpoints').fetchone()[0]
        assert endpoint == f'wss://127.0.0.1:{issuer.mesh}/link', endpoint
        assert db.execute('SELECT state FROM mesh_bootstrap_peers').fetchone()[0] == 'enrolled'
    print('PASS: real /link invite and --mesh-join persist distinct HTTPS/WSS endpoints')


def security_tests(a, b, work):
    with sqlite3.connect(a.db) as db:
        saved_trust = db.execute('SELECT * FROM mesh_explicit_trust WHERE owner_uuid=?', (b.node,)).fetchone()
        db.execute('DELETE FROM mesh_explicit_trust WHERE owner_uuid=?', (b.node,))
    wait(lambda: 'Enrolled identity trust revoked or changed' in a.log(), 'active trust revocation', [a,b], timeout=15)
    b.stop()
    with sqlite3.connect(a.db) as db:
        db.execute('INSERT INTO mesh_explicit_trust VALUES(?,?,?,?,?)', saved_trust)
    print('PASS: active trust revocation disconnects an established link')
    wait(lambda: 'Unlinked site beta' in a.log(), 'probe setup disconnect', [a])
    probe = ROOT / 'build/mesh-probe'
    capture = work / 'captured-proof.json'
    def run_probe(mode, cert=None, key=None, identity=None):
        result = subprocess.run([str(probe), str(identity or b.work / 'identity'),
            str(cert or b.cert), str(key or b.key), str(a.cert), str(a.mesh), b.node, b.name,
            mode, str(capture)], env=dict(os.environ, LD_LIBRARY_PATH=str(ROOT)),
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, f'{mode}: {result.stdout} {result.stderr}'
        print(result.stdout.strip())
        time.sleep(.1)
    for mode in ('no-cert', 'protocol', 'preauth', 'oversize', 'timeout', 'record',
                 'replay', 'signature', 'uuid', 'domain', 'binding', 'role', 'reflection', 'hello'):
        run_probe(mode)
    run_probe('key', identity=work / 'wrong-signing-key')
    rogue = Fixture(work, 'rogue')
    run_probe('untrusted-cert', cert=rogue.cert, key=rogue.key)
    with socket.create_connection(('127.0.0.1', a.mesh), timeout=5) as sock:
        sock.sendall(b'GET /link HTTP/1.1\r\nHost: localhost\r\n\r\n')
        try:
            data = sock.recv(4096)
            assert not data.startswith(b'HTTP/'), data
        except ConnectionResetError:
            pass
    with a.context.wrap_socket(socket.create_connection(('127.0.0.1', a.http), timeout=5), server_hostname='alpha') as sock:
        sock.sendall(b'GET /link HTTP/1.1\r\nHost: localhost\r\n\r\n')
        assert b'426' in sock.recv(4096).split(b'\r\n', 1)[0]
    print('PASS: no plaintext fallback and no Mongoose mesh upgrade')
    # Change both saved expected-pin records so bootstrap loading succeeds,
    # forcing the test to reach the actual outbound TLS certificate check.
    with sqlite3.connect(b.db) as db:
        original = db.execute('SELECT tls_spki_sha256 FROM mesh_explicit_trust WHERE owner_uuid=?', (a.node,)).fetchone()[0]
        bad = base64.b64encode(bytes(32)).decode()
        db.execute('UPDATE mesh_explicit_trust SET tls_spki_sha256=? WHERE owner_uuid=?', (bad, a.node))
        db.execute('UPDATE mesh_bootstrap_peers SET tls_spki_sha256=? WHERE node_uuid=?', (bad, a.node))
    baseline = b.log().count('TLS peer certificate rejected')
    b.start()
    wait(lambda: b.log().count('TLS peer certificate rejected') > baseline, 'outbound actual SPKI mismatch', [a,b])
    b.stop()
    with sqlite3.connect(b.db) as db:
        db.execute('UPDATE mesh_explicit_trust SET tls_spki_sha256=? WHERE owner_uuid=?', (original, a.node))
        db.execute('UPDATE mesh_bootstrap_peers SET tls_spki_sha256=? WHERE node_uuid=?', (original, a.node))
    print('PASS: outbound presented SPKI mismatch rejected')
    # Revoke inbound trust while the issuer stays running; saved peer cache
    # must never turn an enrolled identity into a manual trust exception.
    with sqlite3.connect(a.db) as db:
        trust = db.execute('SELECT * FROM mesh_explicit_trust WHERE owner_uuid=?', (b.node,)).fetchone()
        db.execute('DELETE FROM mesh_explicit_trust WHERE owner_uuid=?', (b.node,))
    run_probe('untrusted-cert')
    with sqlite3.connect(a.db) as db:
        db.execute('INSERT INTO mesh_explicit_trust VALUES(?,?,?,?,?)', trust)
    print('PASS: revoked SQLite trust rejected without issuer restart')
    a.stop()
    config = a.config.read_text()
    a.config.write_text(config.replace('[listener:link]\nacl=test-local', '[listener:link]\nacl=test-deny') + '\n[acl:test-deny]\ndeny=all\n')
    a.start()
    run_probe('acl')
    print('PASS: listener ACL enforced before authentication/admission')
    a.stop()
    a.config.write_text(config)


def run():
    with tempfile.TemporaryDirectory(prefix='mesh-native-', dir=ROOT / 'build') as temporary:
        work = pathlib.Path(temporary)
        a, b = Fixture(work, 'alpha'), Fixture(work, 'beta')
        try:
            a.start()
            # Browsers normally offer permessage-deflate. The native endpoint
            # may decline compression but must still negotiate rustyrig.v2.
            with a.context.wrap_socket(socket.create_connection(('127.0.0.1', a.mesh), timeout=3), server_hostname='alpha') as browser:
                key = base64.b64encode(os.urandom(16)).decode()
                browser.sendall((f'GET /ws/ HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n'
                    f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n'
                    'Sec-WebSocket-Protocol: rustyrig.v2\r\n'
                    'Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n\r\n').encode())
                headers = b''
                while b'\r\n\r\n' not in headers: headers += browser.recv(4096)
                assert b'101 Switching Protocols' in headers and b'Sec-WebSocket-Extensions:' not in headers
            enroll(a, b)
            if os.getenv('RR_MESH_FIXED_PORTS'):
                # Simulate a previously enrolled installation: the issuer's
                # saved HTTPS URL and old default native endpoint are both stale.
                with sqlite3.connect(b.db) as db:
                    db.execute("UPDATE mesh_bootstrap_peers SET url='wss://127.0.0.1:4420/link'")
                    db.execute("UPDATE mesh_link_endpoints SET url='wss://127.0.0.1:4421/link'")
            a.stop()
            a.start()
            b.start()
            if os.getenv('RR_MESH_FIXED_PORTS'):
                with sqlite3.connect(b.db) as db:
                    endpoint = db.execute('SELECT url FROM mesh_link_endpoints').fetchone()[0]
                    assert endpoint == 'wss://127.0.0.1:4420/link', endpoint
                assert 'Legacy HTTPS port 4420 moved to 4422' in a.log()
            wait(lambda: 'Linked to beta' in a.log() and 'Linked to alpha' in b.log(), 'mutual mesh link', [a, b])
            client = a.login(native=True)
            client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link', 'data': 'list'}})
            reply = client.until(lambda m: '(beta): linked' in notice_text(m))
            assert b.node in notice_text(reply)
            def catalog_visible():
                client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'list'}})
                listed = client.until(lambda m: m.get('talk', {}).get('cmd') == 'room-list')
                return '#beta' in listed['talk'].get('remote-rooms', '')
            wait(catalog_visible, 'remote catalog', [a, b])
            client.send_payload(b'not-a-binframe', 2)
            client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'list'}})
            client.until(lambda m: m.get('talk', {}).get('cmd') == 'room-list')
            legacy = a.login()
            legacy.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'msg', 'msg_type': 'pub',
                'target': '#alpha', 'data': 'legacy-to-native'}})
            client.until(lambda m: m.get('talk', {}).get('data') == 'legacy-to-native')
            client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'msg', 'msg_type': 'pub',
                'target': '#alpha', 'data': 'native-to-legacy'}})
            legacy.until(lambda m: m.get('talk', {}).get('data') == 'native-to-legacy')
            legacy.socket.close()
            print('PASS: startup mesh, /link list, native client login/LIST and mixed-transport chat')
            old_a = a.log().count('Linked to beta')
            old_b = b.log().count('Linked to alpha')
            b.stop()
            wait(lambda: 'Unlinked site beta' in a.log(), 'link withdrawal', [a])
            b.start()
            wait(lambda: a.log().count('Linked to beta') > old_a and b.log().count('Linked to alpha') > old_b,
                 'automatic reconnect', [a, b])
            client.send({'msg': {'type': 'talk'}, 'talk': {'cmd': 'link', 'data': 'list'}})
            client.until(lambda m: '(beta): linked' in notice_text(m))
            client.socket.close()
            print('PASS: link withdrawal, restart/reconnect and existing client continuity')
            if os.getenv("RR_MESH_SMOKE_ONLY") != "1":
                security_tests(a, b, work)
        except Exception:
            for server in (a, b):
                print(f'--- {server.name} ---\n{server.log()[-16000:]}')
            raise
        finally:
            a.stop()
            b.stop()


if __name__ == '__main__':
    run()

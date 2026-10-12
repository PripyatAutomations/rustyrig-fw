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
from ws_helpers import WebSocket
ROOT = pathlib.Path(__file__).resolve().parents[2]

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

class LinkSocket(WebSocket):
    def __init__(self, port, context, protocol='rustyrig-link.v1'):
        self.socket = context.wrap_socket(socket.create_connection(('127.0.0.1', port), timeout=3), server_hostname='localhost')
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
    context = ssl.create_default_context(cafile=str(cert))
    ports = {name: port() for name in ('alpha', 'beta', 'gamma')}
    processes, outputs = {}, {}
    def peer(name, remote, role, url=True):
        return f'''[{'peer' if name == 'alpha' else 'peer:' + remote}]
enabled=true
autoconnect=true
server.url={'wss://localhost:' + str(ports[remote]) + '/link' if url else ''}
username=tree-test
password=disposable-link-secret
site.name={remote}
role={role}
'''
    def start(name, ca=None):
        database = work / f'{name}.db'
        if not database.exists():
            with sqlite3.connect(database) as db:
                db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
                db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,'TEST',1,?,3,'admin,view,chat')",
                    (hashlib.sha1(b'test-password').hexdigest(),))
        config = work / f'{name}.cfg'
        settings = f'''[general]
station.name={name}
rig.instances=rig0
rig.default=rig0
path.db.master={database}
path.modules={work}/modules
log.file={work}/{name}.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port()}
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
[rig:rig0]
backend=internal
vfos=A
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
'''
        if name == 'alpha': settings += peer(name, 'beta', 'child')
        elif name == 'beta': settings += peer(name, 'alpha', 'parent') + peer(name, 'gamma', 'child', False)
        else: settings += peer(name, 'beta', 'parent')
        # Explicitly disabled incomplete sections must be harmless.
        settings += '[peer:disabled]\nenabled=false\nautoconnect=true\n'
        config.write_text(settings)
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
        # Invalid credentials must close without replying with local credentials.
        bad = LinkSocket(ports['alpha'], context)
        assert b'101' in bad.headers
        bad.send({'op':'link.hello', 'node':'00000000-0000-4000-8000-000000000002',
            'site':'beta', 'root':'', 'username':'tree-test', 'password':'wrong', 'role':'child'})
        try:
            bad.receive()
            raise AssertionError('unauthorized peer received a response')
        except (EOFError, ConnectionResetError): pass
        finally: bad.socket.close()
        wrong_site = LinkSocket(ports['alpha'], context)
        wrong_site.send({'op':'link.hello', 'node':'00000000-0000-4000-8000-000000000002',
            'site':'unexpected-site', 'root':'', 'username':'tree-test',
            'password':'disposable-link-secret', 'role':'child'})
        try:
            wrong_site.receive()
            raise AssertionError('unexpected site was accepted')
        except (EOFError, ConnectionResetError): pass
        finally: wrong_site.socket.close()
        start('beta')
        wait(lambda: 'Linked site beta' in log('alpha') and 'Linked site alpha' in log('beta'), 'mutual two-server link failed')
        # A late authenticated duplicate must preserve the established link.
        node = re.search(r'Server link node ([0-9a-f-]+)', log('beta')).group(1)
        before_duplicates = log('alpha').count('Unlinked site beta')
        duplicate = LinkSocket(ports['alpha'], context)
        duplicate.send({'op':'link.hello', 'node':node, 'site':'beta', 'root':'',
            'username':'tree-test', 'password':'disposable-link-secret', 'role':'child'})
        try:
            duplicate.receive()
            raise AssertionError('late duplicate was accepted')
        except (EOFError, ConnectionResetError): pass
        finally: duplicate.socket.close()
        assert log('alpha').count('Unlinked site beta') == before_duplicates
        # Native outbound TLS must reject an unrelated trust anchor.
        bad_cert, bad_key = work / 'bad-ca.pem', work / 'bad-key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-subj', '/CN=untrusted', '-keyout', str(bad_key), '-out', str(bad_cert)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        start('gamma', bad_cert)
        wait(lambda: 'Transport error' in log('gamma'), 'outbound TLS did not reject untrusted certificate')
        assert 'Linked site beta' not in log('gamma')
        stop('gamma')
        start('gamma')
        wait(lambda: 'Introduced server' in log('alpha') and 'servers 3' in log('alpha') and
            'Linked site beta' in log('gamma') and 'servers 3' in log('gamma'), 'three-server tree did not converge')
        before = log('alpha').count('Linked site beta')
        stop('beta')
        wait(lambda: 'Unlinked site beta, servers 1' in log('alpha') and 'Unlinked site beta, servers 1' in log('gamma'), 'disconnect did not withdraw branch')
        start('beta')
        wait(lambda: log('alpha').count('Linked site beta') > before and
            log('alpha').count('servers 3') >= 2, 'autoconnect did not restore tree')
        assert 'disposable-link-secret' not in ''.join(log(n) for n in processes)
        print('PASS: mutual WSS authentication/CA verification, separate protocol, concurrent dials, incoming-only peer, three-server tree withdrawal and autoconnect')
    finally:
        for name in list(processes): stop(name)

"""Exercise listener ACLs before and after real WebSocket authentication."""
import hashlib
import os
import pathlib
import socket
import sqlite3
import subprocess
import tempfile
import time
from ws_helpers import WebSocket
ROOT = pathlib.Path(__file__).resolve().parents[2]

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def login(c, name):
    c.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'login', 'user': name}})
    a = c.until(lambda m: m.get('auth', {}).get('cmd') == 'challenge')['auth']
    password = hashlib.sha1((hashlib.sha1(b'test-password').hexdigest() + '+' + a['nonce']).encode()).hexdigest()
    c.send({'msg': {'type': 'auth'}, 'auth': {'cmd': 'pass', 'user': name, 'pass': password, 'token': a['token']}})
    return c.until(lambda m: m.get('auth', {}).get('cmd') == 'authorized')

with tempfile.TemporaryDirectory(prefix='rr-acl-live-') as temporary:
    work = pathlib.Path(temporary)
    database = work / 'users.db'
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
        for uid, name, privileges in [(1, 'TEST', 'admin,view,chat'), (2, 'READER', 'view,chat')]:
            db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(?,?,1,?,3,?)',
                (uid, name, hashlib.sha1(b'test-password').hexdigest(), privileges))
    for label, rules, permitted, denied in [
        ('empty', None, [], []),
        ('deny-all', 'deny=all', [], []),
        ('cidr', 'allow=192.0.2.0/24', [], []),
        ('user', 'deny=reader@127.0.0.0/8\nallow=all', ['TEST'], ['READER']),
        ('group', 'allow=@admin', ['TEST'], ['READER']),
        ('first-match', 'allow=all\ndeny=TEST', ['TEST', 'READER'], []),
        ('user-all', 'allow=TEST@all', ['TEST'], ['READER']),
    ]:
        tcp = port()
        config = work / (label + '.cfg')
        text = f'''[general]
station.name=acltest
rig.instances=rig0
rig.default=rig0
path.db.master={database}
path.modules={work}/modules
log.file={work}/{label}.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={tcp}
net.http.tls-enabled=false
net.http.authdb-dynamic=true
net.http.www-root={ROOT}/www
net.http.ua-bans={work}/absent
net.mqtt.enabled=false
net.mqtt-client.enabled=false
atu.max=0
quota.enforce=false
chat.replay-lines=0
[rig:rig0]
backend=internal
vfos=A
[callsign-lookup]
enabled=false
'''
        if rules:
            text += f'[acl:policy]\n{rules}\n[listener:http]\nacl=policy\n'
        config.write_text(text)
        with (work / 'console').open('w') as output:
            process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=ROOT,
                stdout=output, stderr=subprocess.STDOUT, env=dict(os.environ, LD_LIBRARY_PATH=str(ROOT)))
            try:
                until = time.monotonic() + 10
                while True:
                    assert process.poll() is None, (work / 'console').read_text()
                    try:
                        with socket.create_connection(('127.0.0.1', tcp), timeout=.2): pass
                        break
                    except ConnectionRefusedError:
                        assert time.monotonic() < until
                        time.sleep(.05)
                if not permitted and not denied:
                    with socket.create_connection(('127.0.0.1', tcp), timeout=3) as probe:
                        probe.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n')
                        try: assert probe.recv(4096) == b''
                        except ConnectionResetError: pass
                for name in permitted:
                    c = WebSocket(tcp)
                    try: assert login(c, name)['auth']['cmd'] == 'authorized'
                    finally: c.socket.close()
                for name in denied:
                    c = WebSocket(tcp)
                    try:
                        try:
                            login(c, name)
                            raise AssertionError('ACL denied account was authorized')
                        except (EOFError, ConnectionResetError): pass
                    finally: c.socket.close()
                print(f'PASS: real HTTP/WS listener ACL {label}', flush=True)
            finally:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()

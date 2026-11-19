"""Exercise exported shares over the production WebSocket protocol."""
import base64
import hashlib
import os
import pathlib
import socket
import ssl
import struct
import sqlite3
import subprocess
import tempfile
import time
from ws_helpers import WebSocket, TEST_ACLS
ROOT = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='rr-files-live-') as temporary:
    work = pathlib.Path(temporary)
    (work / 'modules').mkdir()
    identity = work / 'identity'
    identity.mkdir(mode=0o700)
    identity.chmod(0o700)
    share = work / 'export'; share.mkdir()
    (share / 'existing.cfg').write_text('original\n')
    (share / 'nested').mkdir()
    (work / 'private').write_text('secret')
    (share / 'escape').symlink_to(work)
    (share / 'private-link').symlink_to(work / 'private')
    cert = work / 'tls-cert.pem'
    key = work / 'tls-key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
        '-keyout', str(key), '-out', str(cert), '-days', '1', '-sha256',
        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:filestest.files-test.local,DNS:localhost,IP:127.0.0.1'],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    tls = ssl.create_default_context(cafile=str(cert))
    dbpath = work / 'master.db'
    with sqlite3.connect(dbpath) as db:
        db.executescript((ROOT / 'sql/sqlite.master.sql').read_text())
        for uid, name, permissions in [(1,'TEST','files.read,files.write,view,webcam'),(2,'READER','files.read'),(3,'DENIED','admin,owner')]:
            db.execute('INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(?,?,1,?,3,?)',
                       (uid,name,hashlib.sha1(b'test-password').hexdigest(),permissions))
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0)); port = reservation.getsockname()[1]
    config = work / 'server.cfg'
    config.write_text(f'''[general]
domain=files-test.local
station.name=filestest
rig.instances=rig0
rig.default=rig0
path.db.master={dbpath}
path.identity={identity}
path.modules={work}/modules
log.file=-
log.level=*:info
net.http.bind=127.0.0.1
net.http.tls-bind=127.0.0.1
net.http.tls-port={port}
net.http.tls-enabled=true
net.http.tls-cert={cert}
net.http.tls-key={key}
net.http.authdb-dynamic=true
net.http.ua-bans={work}/missing-bans
net.mqtt.enabled=false
net.mqtt-client.enabled=false
media.direct.enabled=false
atu.max=0
chat.replay-lines=0
webcam.enable=true
[rig:rig0]
backend=internal
vfos=A
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
[share]
name=public
path={share}
[share:readonly]
path={share}
write=never.granted
''' + TEST_ACLS)
    clients = []
    output = (work / 'console.log').open('w')
    process = subprocess.Popen([str(ROOT/'bin/rrserver'),'-f',str(config)],cwd=ROOT,stdout=output,stderr=subprocess.STDOUT)
    sequence = 0
    def connect(name):
        deadline = time.monotonic()+10
        while True:
            try: c=WebSocket(port, tls); break
            except ConnectionRefusedError:
                if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError((work/'console.log').read_text())
                time.sleep(.05)
        clients.append(c)
        c.send({'msg':{'type':'auth'},'auth':{'cmd':'login','user':name}})
        challenge=c.until(lambda m:m.get('auth',{}).get('cmd')=='challenge')['auth']
        assert challenge['method'] == 'tls-password-v1', challenge
        c.send({'msg':{'type':'auth'},'auth':{'cmd':'pass','user':name,'pass':'test-password',
            'method':challenge['method'],'token':challenge['token']}})
        c.until(lambda m:m.get('auth',{}).get('cmd')=='authorized'); return c
    def request(c, command, path='/', error=False, **fields):
        global sequence
        sequence+=1; identity='files-test-'+str(sequence)
        c.send({'msg':{'type':'file'},'file':{'cmd':command,'path':path,**fields},'request':{'id':identity}})
        rows=[]
        while True:
            msg=c.until(lambda m:m.get('request',{}).get('id')==identity)['file']
            if msg['cmd']=='entry': rows.append(msg); continue
            assert bool(msg.get('error'))==error,msg
            return rows if command=='list' and not error else msg
    try:
        user=connect('TEST'); reader=connect('READER'); denied=connect('DENIED')
        assert {r['name'] for r in request(user,'list')}=={'public','readonly'}
        assert request(denied,'list')==[]
        user.send({'msg':{'type':'object'},'object':{'cmd':'inventory'},'request':{'id':'all-resources'}})
        resources=[]
        def complete(message):
            if message.get('request',{}).get('id')!='all-resources':return False
            if message.get('object',{}).get('cmd')=='inventory-entry':resources.append(message['inventory'])
            return message.get('object',{}).get('cmd')=='inventory-end'
        user.until(complete)
        assert any(row.get('subsystem')==2 and row.get('name')=='station.webcam.rx' for row in resources),resources
        assert {row['name'] for row in resources if row.get('kind')=='share'}=={'public','readonly'},resources
        entries=request(user,'list','/public')
        assert {'existing.cfg','nested'}=={r['name'] for r in entries},entries
        assert 'export' not in str(entries) and str(work) not in str(entries)
        for path in ['/public/../private','/public/escape/private','/public/private-link','/public//existing.cfg','/public/./existing.cfg']:
            request(user,'read',path,error=True)
        request(reader,'begin','/public/nope',error=True)
        request(user,'begin','/readonly/nope',error=True)
        request(user,'delete','/public',error=True)
        request(user,'rename','/public/existing.cfg',dest='/public',error=True)
        payload=bytes(range(256))*400
        request(user,'begin','/public/upload.bin')
        assert not (share/'upload.bin').exists()
        for offset in range(0,len(payload),24576):
            request(user,'chunk','/public/upload.bin',offset=offset,data=base64.b64encode(payload[offset:offset+24576]).decode())
        request(user,'commit','/public/upload.bin')
        assert (share/'upload.bin').read_bytes()==payload
        result=bytearray()
        while len(result)<len(payload):
            reply=request(reader,'read','/public/upload.bin',offset=len(result)); result+=base64.b64decode(reply['data'])
            assert reply['eof']==(len(result)==len(payload))
        assert result==payload

        # Native binary FILE frames: accept only a UUID-bound session,
        # network-order offset and bounded payload. Control/commit stays on
        # the strict compact text wire; file bytes never use JSON/base64.
        binary_id='e2a9bd9a-59f6-4e7b-9f4e-0be9dd0f2e62'
        binary_data=bytes(range(256))*12
        def control_result(client, ident):
            row=client.until(lambda m:m.get('request',{}).get('id')==ident and
                m.get('file',{}).get('cmd')=='result')
            assert not row['file'].get('error'),row
        user.send({'msg':{'type':'file'},'file':{'cmd':'begin','path':'/public/native.bin','binary':True},
                   'request':{'id':binary_id}})
        control_result(user,binary_id)
        for offset in range(0,len(binary_data),24576):
            chunk=binary_data[offset:offset+24576]
            body=binary_id.encode('ascii')+offset.to_bytes(8,'big')+b'\x00'+chunk
            header=b'RR'+bytes([2,5])+b'fl01'+bytes([1,255,255,0])+struct.pack('!IIQ',0,len(body),0)
            assert len(header)==28
            user.send_payload(header+body,opcode=2)
            control_result(user,binary_id)
        user.send({'msg':{'type':'file'},'file':{'cmd':'commit','path':'/public/native.bin'},
                   'request':{'id':binary_id}})
        control_result(user,binary_id)
        assert (share/'native.bin').read_bytes()==binary_data

        download_id='588f1908-31c1-4ca3-a7e2-0bf994984ad7'
        reader.send({'msg':{'type':'file'},
                     'file':{'cmd':'read','path':'/public/native.bin','offset':0,'binary':True},
                     'request':{'id':download_id}})
        opcode,wire=reader.until_frame(lambda op,p:op==2 and len(p)>=73 and p[:4]==b'RR\x02\x05'
                                      and p[4:8]==b'fl01')
        assert opcode==2 and wire[8:12]==bytes([0,255,255,0])
        assert struct.unpack('!I',wire[16:20])[0]==len(wire)-28
        assert wire[28:64]==download_id.encode('ascii')
        assert int.from_bytes(wire[64:72],'big')==0 and wire[72]==1
        assert wire[73:]==binary_data

        request(user,'begin','/public/existing.cfg',error=True)
        request(user,'begin','/public/existing.cfg',overwrite=True)
        request(user,'chunk','/public/existing.cfg',offset=0,data=base64.b64encode(b'edited\n').decode())
        assert (share/'existing.cfg').read_text()=='original\n'
        request(user,'commit','/public/existing.cfg')
        assert (share/'existing.cfg').read_text()=='edited\n'
        request(user,'mkdir','/public/folder')
        request(user,'rename','/public/upload.bin',dest='/public/folder/moved.bin')
        request(user,'rename','/public/existing.cfg',dest='/public/folder/moved.bin',error=True)
        request(user,'delete','/public/folder',error=True)
        request(user,'delete','/public/folder/moved.bin')
        request(user,'delete','/public/folder')
        request(user,'begin','/public/invalid.bin')
        request(user,'chunk','/public/invalid.bin',offset=0,data='%%%bad',error=True)
        assert not (share/'invalid.bin').exists()
        request(user,'begin','/public/revoked.bin')
        with sqlite3.connect(dbpath) as db:db.execute("UPDATE users SET permissions='files.read' WHERE name='TEST'")
        request(user,'chunk','/public/revoked.bin',offset=0,data='YQ==',error=True)
        assert not list(share.glob('.rr-upload-*'))
        with sqlite3.connect(dbpath) as db:db.execute("UPDATE users SET permissions='files.read,files.write,view,webcam' WHERE name='TEST'")
        request(user,'begin','/public/interrupted.bin')
        user.socket.close(); clients.remove(user)
        deadline=time.monotonic()+3
        while list(share.glob('.rr-upload-*')) and time.monotonic()<deadline:time.sleep(.05)
        assert not list(share.glob('.rr-upload-*'))
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT count(*) FROM audit_log WHERE event_type='files'").fetchone()[0]>=6
        print('PASS: virtual exports, permissions, traversal/symlink rejection, chunked binary transfer, atomic editing, rename/move/delete, audit and disconnect cleanup')
    except Exception:
        print((work/'console.log').read_text()); raise
    finally:
        for c in clients:c.socket.close()
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait()
        output.close()

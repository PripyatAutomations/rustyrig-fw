"""Production gpsd + NMEA modules, scoped PTYs and rig GPS WebSocket snapshots."""
import hashlib
import json
import os
import pathlib
import select
import socket
import sqlite3
import subprocess
import struct
import tempfile
import time
from ws_helpers import WebSocket

ROOT = pathlib.Path(__file__).resolve().parents[2]
def nmea(body):
    checksum = 0
    for byte in body.encode():
        checksum ^= byte
    return f"${body}*{checksum:02X}\r\n".encode()
def login(client):
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": "TEST"}})
    challenge = client.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
    password = hashlib.sha1((hashlib.sha1(b"test-password").hexdigest() + "+" + challenge["nonce"]).encode()).hexdigest()
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": "TEST", "pass": password, "token": challenge["token"]}})
    client.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")
def join(client, room):
    client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": room}})
    client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == room)
def snapshot(client, channel):
    client.send({"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": channel}})
    client.until(lambda m: m.get("media", {}).get("cmd") == "subscribed" and m["media"]["chan-uuid"] == channel)
    _, frame = client.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b"RR\x01\x04gpsp")
    return frame[10], struct.unpack("!iiB", frame[28:])


def inventory(client):
    client.send({"msg": {"type": "object"}, "object": {"cmd": "inventory"}, "request": {"id": "test-inventory"}})
    rows = []
    while True:
        message = client.until(lambda m: m.get("request", {}).get("id") == "test-inventory")
        if message.get("object", {}).get("cmd") == "inventory-end":
            return rows
        assert message["object"]["cmd"] == "inventory-entry", message
        rows.append(message["inventory"])

with tempfile.TemporaryDirectory(prefix="rr-gps-live-") as temporary:
    work = pathlib.Path(temporary)
    database = work / "master.db"
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / "sql/sqlite.master.sql").read_text())
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)",
                   ("TEST", 1, hashlib.sha1(b"test-password").hexdigest(), 3, "admin,view,radio,edit,chat"))
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    daemon = socket.socket()
    daemon.bind(("127.0.0.1", 0))
    daemon.listen(1)
    daemon.settimeout(8)
    config = work / "rrserver.cfg"
    config.write_text(f"""[general]
station.name=gpstest
rig.instances=rig0 rig1 rig2
rig.default=rig0
path.db.master={database}
path.modules={ROOT}/bin
log.file={work}/server.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port}
net.http.tls-enabled=false
net.mqtt.enabled=false
gpsd.url=tcp://127.0.0.1:{daemon.getsockname()[1]}
gpsd.target=station
[rig:rig0]
backend=internal
vfos=A
gps.position=38.1234567,-80.7654321
[rig:rig1]
backend=internal
vfos=A
[rig:rig2]
backend=internal
vfos=A
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
[modules]
rrserver-gpsd=
rrserver-gps-nmea=
[serial]
ttyGPS0=rig2.gps-in@4800
ttyGPS1=station.gps-out@4800
ttyGPSfixed=rig0.gps-in@4800
ttyGPSraw=station.gps-out@4800
[serial:ttyGPS0]
path={work}/ttyGPS0
[serial:ttyGPS1]
path={work}/ttyGPS1
[serial:ttyGPSraw]
path={work}/ttyGPSraw
gps-output=nmea
[serial:ttyGPSfixed]
path={work}/disabled-fixed-input
""")
    client = None
    peer = None
    gps_in = gps_out = gps_raw = None
    with (work / "console.log").open("w") as output:
        process = subprocess.Popen([str(ROOT / "bin/rrserver"), "-f", str(config)], cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        try:
            peer, _ = daemon.accept()
            peer.settimeout(5)
            watch = b""
            while b";\n" not in watch:
                watch += peer.recv(1024)
            assert watch.startswith(b"?WATCH=")
            policy = json.loads(watch[7:].rstrip(b";\n"))
            assert policy["nmea"] is True and policy["raw"] == 1 and policy["json"] is False
            assert not (work / "disabled-fixed-input").exists()
            gps_in = os.open(work / "ttyGPS0", os.O_RDWR | os.O_NONBLOCK)
            gps_out = os.open(work / "ttyGPS1", os.O_RDWR | os.O_NONBLOCK)
            gps_raw = os.open(work / "ttyGPSraw", os.O_RDWR | os.O_NONBLOCK)
            # Fragmented input and oversize/bad-checksum records resynchronize.
            peer.sendall(b'{"class":"VERSION"}\n' + b'$' + b'x' * 520 + b'\n$GPGLL*00\n')
            position = nmea('GPRMC,123519,A,4807.038,N,01131.000,E,0.0,,230394,,,A')
            peer.sendall(position[:17])
            peer.sendall(position[17:])
            os.write(gps_in, nmea('GPGGA,123519,3351.000,S,15112.000,E,1,08,0.9,0.0,M,0.0,M,,'))
            assert select.select([gps_out], [], [], 5)[0]
            generated = os.read(gps_out, 1024)
            assert b',A,4807.038000,N,01131.000002,E,' in generated
            assert select.select([gps_raw], [], [], 5)[0]
            assert position in os.read(gps_raw, 1024)
            satellites = nmea('GPGSV,1,1,00')
            peer.sendall(satellites)
            assert select.select([gps_raw], [], [], 5)[0]
            assert satellites in os.read(gps_raw, 1024)
            time.sleep(0.1)
            assert not select.select([gps_out], [], [], 0.1)[0], 'GPS must not stream continuously'
            client = WebSocket(port)
            login(client)
            client.send({"msg": {"type": "media"}, "media": {"cmd": "list"}})
            channels = {}
            while len(channels) < 4:
                message = client.until(lambda m: m.get("media", {}).get("cmd") == "available" and m["media"].get("codec") == "gpsp")
                media = message['media']
                channels[media['name']] = (media['chan-uuid'], media['rig'])
                if media['name'] == 'station.gps.rx':
                    assert media['room'] == '#gpstest' and media['joined'] is True
            rows = inventory(client)
            assert rows[0]['kind'] == 'site' and rows[0]['uuid']
            assert len([r for r in rows if r['kind'] == 'rig']) == 3
            assert len([r for r in rows if r['kind'] == 'vfo']) >= 3
            gps_row = next(r for r in rows if r['name'] == 'rig0.gps-out')
            assert gps_row['coordinates'] == '38.1234567,-80.7654321'
            assert gps_row['uuid'] == channels['rig0.gps.rx'][0]
            assert any(r['name'] == 'rig0.gps-in' and r['state'] == 'disabled-by-fixed-position' for r in rows)
            # Discovery is not permission to consume another room's media.
            client.send({"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": gps_row['uuid']}})
            client.until(lambda m: m.get('msg', {}).get('type') == 'error')
            index, data = snapshot(client, channels['station.gps.rx'][0])
            assert index == 255 and data == (481173000, 115166667, 1)
            client.send({"msg": {"type": "media"}, "media": {"cmd": "list"}})
            raw_channel = client.until(lambda m: m.get("media", {}).get("name") == "station.nmea.rx")['media']['chan-uuid']
            client.send({"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": raw_channel}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'subscribed' and m['media']['chan-uuid'] == raw_channel)
            peer.sendall(satellites)
            _, raw_frame = client.until_frame(lambda op, data: op == 2 and data[:8] == b"RR\x01\x04nmea")
            assert raw_frame[28:] == satellites.rstrip(b'\r\n')
            client.send({"msg": {"type": "media"}, "media": {"cmd": "unsubscribe", "chan-uuid": raw_channel}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'unsubscribed')
            join(client, '#gpstest-rig0')
            index, data = snapshot(client, channels['rig0.gps.rx'][0])
            assert index == channels['rig0.gps.rx'][1] and data == (381234567, -807654321, 3)
            client.send({"msg": {"type": "media"}, "media": {"cmd": "codec", "chan-uuid": channels['rig0.gps.rx'][0], "codec": "pc16"}})
            client.until(lambda m: m.get('msg', {}).get('type') == 'error')
            join(client, '#gpstest-rig1')
            index, data = snapshot(client, channels['rig1.gps.rx'][0])
            assert index == channels['rig1.gps.rx'][1] and data == (481173000, 115166667, 1)
            join(client, '#gpstest-rig2.rx')
            index, data = snapshot(client, channels['rig2.gps.rx'][0])
            assert index == channels['rig2.gps.rx'][1] and data == (-338500000, 1512000000, 1)
            # Unsubscribe/resubscribe provides the current snapshot without the 5m wait.
            client.send({"msg": {"type": "media"}, "media": {"cmd": "unsubscribe", "chan-uuid": channels['rig0.gps.rx'][0]}})
            client.until(lambda m: m.get('media', {}).get('cmd') == 'unsubscribed')
            index, data = snapshot(client, channels['rig0.gps.rx'][0])
            assert index == channels['rig0.gps.rx'][1] and data[2] == 3
            print('PASS: production GPS modules, gpsd WATCH, scoped PTYs, generated rig coordinates, fallback and switch snapshots')
            # Configured station coordinates prevent the daemon connection too.
            client.socket.close()
            client = None
            process.terminate()
            process.wait(timeout=5)
            peer.close()
            peer = None
            config.write_text(config.read_text().replace('station.name=gpstest',
                'station.name=gpstest\nstation.gps.position=42,-71'))
            process = subprocess.Popen([str(ROOT / 'bin/rrserver'), '-f', str(config)], cwd=ROOT,
                stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 8
            while True:
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('fixed-position server failed to start')
                try:
                    client = WebSocket(port)
                    break
                except ConnectionRefusedError:
                    time.sleep(0.05)
            login(client)
            client.send({'msg': {'type': 'media'}, 'media': {'cmd': 'list'}})
            metadata = client.until(lambda m: m.get('media', {}).get('name') == 'station.gps.rx')['media']
            index, data = snapshot(client, metadata['chan-uuid'])
            assert index == 255 and data == (420000000, -710000000, 3)
            daemon.settimeout(0.2)
            try:
                unwanted, _ = daemon.accept()
            except TimeoutError:
                pass
            else:
                unwanted.close()
                raise AssertionError('fixed station coordinates must disable gpsd connection')
            assert not (work / 'disabled-fixed-input').exists()
            print('PASS: configured coordinates disable live gpsd/NMEA inputs and generate manual logger positions')
        except Exception:
            print((work / 'console.log').read_text())
            raise
        finally:
            if client:
                client.socket.close()
            if peer:
                peer.close()
            daemon.close()
            for fd in (gps_in, gps_out, gps_raw):
                if fd is not None:
                    os.close(fd)
            process.terminate()
            process.wait(timeout=5)

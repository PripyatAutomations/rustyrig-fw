"""Exercise serial commands and MODEM/seri payloads through real WebSockets."""
import json
import hashlib
import os
import pathlib
import pty
import select
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import tty
from ws_helpers import WebSocket

ROOT = pathlib.Path(__file__).resolve().parents[2]

def login(client, username="TEST"):
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": username}})
    challenge = client.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
    password = hashlib.sha1((hashlib.sha1(b"test-password").hexdigest() + "+" + challenge["nonce"]).encode()).hexdigest()
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": username, "pass": password, "token": challenge["token"]}})
    client.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")

def command(client, cmd, **fields):
    if "path" in fields:
        client.send_payload(json.dumps({"op": "serial." + cmd, "name": "ttyHOST0", **fields}).encode(), 1)
    else:
        client.send({"msg": {"type": "serial"}, "serial": {"cmd": cmd, "name": "ttyHOST0", **fields}})

def serial(client, cmd):
    return client.until(lambda m: m.get("serial", {}).get("cmd") == cmd)["serial"]

def device_read(fd, length):
    result = b""
    deadline = time.monotonic() + 4
    while len(result) < length:
        remaining = deadline - time.monotonic()
        assert remaining > 0
        assert select.select([fd], [], [], remaining)[0]
        result += os.read(fd, length - len(result))
    return result


def inventory(client):
    client.send({"msg": {"type": "object"}, "object": {"cmd": "inventory"}, "request": {"id": "test-inventory"}})
    rows = []
    while True:
        message = client.until(lambda m: m.get("request", {}).get("id") == "test-inventory")
        if message.get("object", {}).get("cmd") == "inventory-end":
            return rows
        assert message["object"]["cmd"] == "inventory-entry", message
        rows.append(message["inventory"])

with tempfile.TemporaryDirectory(prefix="rr-serial-live-") as temporary:
    work = pathlib.Path(temporary)
    database = work / "master.db"
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / "sql/sqlite.master.sql").read_text())
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)",
                   ("TEST", 1, hashlib.sha1(b"test-password").hexdigest(), 3, "serial,view,radio,edit,chat"))
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(2,?,?,?,?,?)",
                   ("RESTRICTED", 1, hashlib.sha1(b"test-password").hexdigest(), 3, "admin,owner,view,radio,chat"))
        for uid, name, flags in [(3, "EXACT", "serial.ttyHOST0"), (4, "PREFIX", "serial.ttyHOST*"), (5, "WRONG", "serial.ttyGPS*")]:
            db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(?,?,1,?,3,?)",
                (uid, name, hashlib.sha1(b"test-password").hexdigest(), flags))
    master, slave = pty.openpty()
    tty.setraw(slave)
    device = os.ttyname(slave)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    config = work / "rrserver.cfg"
    config.write_text(f"""[general]
station.name=serialtest
rig.instances=rig0
rig.default=rig0
path.db.master={database}
path.modules={work}/modules
log.file={work}/server.log
log.level=*:info
net.http.bind=127.0.0.1
net.http.port={port}
net.http.tls-enabled=false
net.http.authdb-dynamic=true
net.http.www-root={ROOT}/www
net.http.ua-bans={work}/absent-ua-bans
net.mqtt.enabled=false
net.mqtt-client.enabled=false
atu.max=0
chat.replay-lines=0
[rig:rig0]
backend=internal
vfos=A
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
[serial]
ttyHOST0=serial:{device}@115200,8n1
[serial:ttyHOST0]
buffer-bytes=0
""")
    clients = []
    with (work / "console.log").open("w") as output:
        process = subprocess.Popen([str(ROOT / "bin/rrserver"), "-f", str(config)], cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while True:
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError((work / "console.log").read_text())
                try:
                    first = WebSocket(port)
                    clients.append(first)
                    break
                except ConnectionRefusedError:
                    time.sleep(0.05)
            command(first, "open", port="ttyHOST0")
            first.until(lambda m: m.get("msg", {}).get("type") == "error")
            login(first)
            rows = inventory(first)
            export = next(r for r in rows if r['kind'] == 'serial' and r['name'] == 'ttyHOST0')
            assert export['state'] == 'available' and 'path' not in export
            assert device not in str(rows)
            command(first, "list")
            available = serial(first, "available")
            assert available["name"] == "ttyHOST0" and available["baud"] == 115200
            assert available["port"] == "ttyHOST0" and "path" not in available
            command(first, "open", path=device)
            first.until(lambda m: m.get("msg", {}).get("type") == "error")
            command(first, "open", port="ttyHOST0", path=device)
            first.until(lambda m: m.get("msg", {}).get("type") == "error")
            command(first, "open", port="ttyHOST0")
            opened = serial(first, "opened")
            stream = opened["stream"]
            assert 0 < stream < 256 and opened["mode"] == "8n1"
            payload = bytes(range(256))
            header = struct.pack("!2sBB4sBBBBIIQ", b"RR", 1, 4, b"seri", 1, 255, 255, stream, 1, len(payload), 0)
            first.send_payload(header + payload)
            assert serial(first, "written")["seq"] == 1
            assert device_read(master, len(payload)) == payload
            reply = payload[::-1]
            assert os.write(master, reply) == len(reply)
            _, frame = first.until_frame(lambda opcode, data: opcode == 2 and data[:8] == b"RR\x01\x04seri")
            assert frame[8:12] == bytes([0, 255, 255, stream]) and frame[28:] == reply
            command(first, "read", seq=1, stream=stream)
            command(first, "configure", baud=19200, mode="8n2", stream=stream)
            configured = serial(first, "configured")
            assert configured["baud"] == 19200 and configured["mode"] == "8n2"
            command(first, "configure", baud="oops", mode="8n1", stream=stream)
            assert serial(first, "error")["error"] == "settings-failed"
            # An acknowledged old stream must still not be reused. Replay
            # its first frame after reopening, when sequence 1 is valid again.
            command(first, "close", stream=stream)
            serial(first, "closed")
            command(first, "open", port="ttyHOST0")
            reopened = serial(first, "opened")
            assert reopened["stream"] != stream
            first.send_payload(header + payload)
            command(first, "list")
            serial(first, "list-end")
            assert not select.select([master], [], [], 0.1)[0], "stale bytes reached reopened device"
            stream = reopened["stream"]
            header = struct.pack("!2sBB4sBBBBIIQ", b"RR", 1, 4, b"seri", 1, 255, 255, stream, 1, len(payload), 0)
            first.send_payload(header + payload)
            assert serial(first, "written")["seq"] == 1
            assert device_read(master, len(payload)) == payload
            for fields in ({"stream": stream + 256}, {"stream": 1.5}, {"seq": 4294967296}):
                command(first, "close", **fields)
                assert serial(first, "error")["error"] == "invalid-request"
            first.send_payload(json.dumps({"op": "serial.configure", "name": "ttyHOST0", "baud": 18446744073709551615, "stream": stream}).encode(), 1)
            first.until(lambda m: m.get("msg", {}).get("type") == "error")
            second = WebSocket(port)
            clients.append(second)
            login(second)
            command(second, "open", port="ttyHOST0")
            assert serial(second, "error")["error"] == "device-busy"
            command(second, "open", port="not-exported")
            assert serial(second, "error")["error"] == "forbidden-device"
            # All 255 nonzero IDs are usable once, never recycled.
            for expected_stream in range(stream + 1, 256):
                command(first, "close")
                serial(first, "closed")
                command(first, "open", port="ttyHOST0")
                assert serial(first, "opened")["stream"] == expected_stream
            command(first, "close")
            serial(first, "closed")
            command(first, "open", port="ttyHOST0")
            assert serial(first, "error")["error"] == "stream-limit-reconnect"
            first.socket.close()
            time.sleep(0.05)
            command(second, "open", port="ttyHOST0")
            assert serial(second, "opened")["stream"] > 0
            command(second, "close")
            serial(second, "closed")
            # A newly authenticated user without serial privileges cannot
            # discover or open a physical export, but still sees rig GPS.
            # Users are loaded at startup (or explicit rehash), so seed the
            # restricted account before launching instead of editing SQLite
            # behind the running server's cached authentication table.
            restricted = WebSocket(port)
            clients.append(restricted)
            login(restricted, "RESTRICTED")
            rows = inventory(restricted)
            assert not any(r['kind'] == 'serial' for r in rows)
            assert any(r['name'] == 'rig0.gps-out' for r in rows)
            command(restricted, "open", port="ttyHOST0")
            assert serial(restricted, "error")['error'] == 'forbidden-device'
            for name, permitted in [("EXACT", True), ("PREFIX", True), ("WRONG", False)]:
                account = WebSocket(port)
                clients.append(account)
                login(account, name)
                rows = inventory(account)
                assert any(r['kind'] == 'serial' and r['name'] == 'ttyHOST0' for r in rows) == permitted
                command(account, "open", port="ttyHOST0")
                if permitted:
                    serial(account, "opened")
                    command(account, "close")
                    serial(account, "closed")
                else:
                    assert serial(account, "error")['error'] == 'forbidden-device'
            print("PASS: production serial auth, named exports, MODEM binary bytes, settings, ownership and session cleanup")
        except Exception:
            print((work / "console.log").read_text())
            raise
        finally:
            for client in clients:
                client.socket.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            os.close(master)
            os.close(slave)

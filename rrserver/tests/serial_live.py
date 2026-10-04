"""Exercise serial commands and MODEM/seri payloads through real WebSockets."""
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

def login(client):
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": "TEST"}})
    challenge = client.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
    password = hashlib.sha1((hashlib.sha1(b"test-password").hexdigest() + "+" + challenge["nonce"]).encode()).hexdigest()
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": "TEST", "pass": password, "token": challenge["token"]}})
    client.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")

def command(client, cmd, **fields):
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

with tempfile.TemporaryDirectory(prefix="rr-serial-live-") as temporary:
    work = pathlib.Path(temporary)
    database = work / "master.db"
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / "sql/sqlite.master.sql").read_text())
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)",
                   ("TEST", 1, hashlib.sha1(b"test-password").hexdigest(), 3, "admin,view,radio,edit,chat"))
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
            command(first, "list")
            available = serial(first, "available")
            assert available["name"] == "ttyHOST0" and available["baud"] == 115200
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
            second = WebSocket(port)
            clients.append(second)
            login(second)
            command(second, "open", port="ttyHOST0")
            assert serial(second, "error")["error"] == "device-busy"
            command(second, "open", port="not-exported")
            assert serial(second, "error")["error"] == "forbidden-device"
            first.socket.close()
            time.sleep(0.05)
            command(second, "open", port="ttyHOST0")
            assert serial(second, "opened")["stream"] > 0
            command(second, "close")
            serial(second, "closed")
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

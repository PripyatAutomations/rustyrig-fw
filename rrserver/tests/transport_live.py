"""Exercise HTTP, malformed WebSocket messages and CAT isolation on disposable rigs."""
import json
import hashlib
import http.client
import pathlib
import socket
import sqlite3
import subprocess
import tempfile
import time
from ws_helpers import WebSocket

ROOT = pathlib.Path(__file__).resolve().parents[2]


def login(client, name):
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": name}})
    a = client.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
    password = hashlib.sha1((hashlib.sha1(b"test-password").hexdigest() + "+" + a["nonce"]).encode()).hexdigest()
    client.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": name, "pass": password, "token": a["token"]}})
    client.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")
    client.until(lambda m: m.get("talk", {}).get("cmd") == "join")
    client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#transport-rig0"}})
    client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == "#transport-rig0")


def drain(client):
    seen = []
    client.send({"msg": {"type": "ping", "ts": 1234567}})
    def barrier(m):
        seen.append(m)
        return m.get("msg", {}).get("type") == "pong" and m["msg"].get("ts") == 1234567
    client.until(barrier)
    return seen


def cat(client, cmd, **fields):
    client.send_payload(json.dumps({"op": "cat." + cmd, "room": "#transport-rig0", "vfo": "A", **fields}).encode(), 1)


with tempfile.TemporaryDirectory(prefix="rr-transport-audit-") as temporary:
    work = pathlib.Path(temporary)
    webroot = work / "www"
    webroot.mkdir()
    (webroot / "index.html").write_text("safe index")
    (webroot / "asset.js").write_text("safe asset")
    (webroot / ".private").write_text("private metadata")
    outside = work / "outside.txt"
    outside.write_text("outside secret")
    (webroot / "escape.txt").symlink_to(outside)
    (webroot / "compressed.js.gz").symlink_to(outside)
    (webroot / "inside.js").symlink_to("asset.js")
    folder = webroot / "folder"
    folder.mkdir()
    (folder / "index.html").symlink_to(outside)
    database = work / "master.db"
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / "sql/sqlite.master.sql").read_text())
        for uid, name, privileges in [(1, "ADMIN", "admin,tx,chat"), (2, "TX", "tx,chat"), (3, "LIMITED", "tx,chat"), (4, "OWNER", "owner,chat"), (5, "ELMER", "elmer,chat"), (6, "NOOB", "noob,chat")]:
            db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(?,?,1,?,3,?)",
                (uid, name, hashlib.sha1(b"test-password").hexdigest(), privileges))
        db.execute("INSERT INTO tx_credits(name,credits) VALUES('ADMIN',1000)")
        db.execute("INSERT INTO tx_credits(name,credits) VALUES('TX',1000)")
        for name in ["OWNER", "NOOB"]:
            db.execute("INSERT INTO tx_credits(name,credits) VALUES(?,1000)", (name,))
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    config = work / "rrserver.cfg"
    config.write_text(f"""[general]
station.name=transport
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
net.http.www-root={webroot}
net.http.ua-bans={work}/absent-ua-bans
net.mqtt.enabled=false
net.mqtt-client.enabled=false
quota.enforce=true
noob.cool-down=0
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
""")
    clients = []
    with (work / "console.log").open("w") as output:
        process = subprocess.Popen([str(ROOT / "bin/rrserver"), "-f", str(config)], cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while True:
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError((work / "console.log").read_text())
                try:
                    first = WebSocket(port)
                    clients.append(first)
                    break
                except ConnectionRefusedError:
                    time.sleep(0.05)
            def http_request(path, expected, method="GET"):
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
                c.request(method, path)
                response = c.getresponse()
                body = response.read()
                assert response.status == expected, (path, response.status, body)
                assert b"outside secret" not in body and b"private metadata" not in body
                c.close()
                return body
            for protocol in (None, 'rustyrig.v0'):
                connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                headers = {'Upgrade': 'websocket', 'Connection': 'Upgrade',
                    'Sec-WebSocket-Version': '13', 'Sec-WebSocket-Key': 'dGhlIHNhbXBsZSBub25jZQ=='}
                if protocol: headers['Sec-WebSocket-Protocol'] = protocol
                connection.request('GET', '/ws', headers=headers)
                response = connection.getresponse()
                assert response.status == 426
                assert response.getheader('Sec-WebSocket-Protocol') == 'rustyrig.v1'
                response.read(); connection.close()
            assert http_request("/", 200) == b"safe index"
            assert http_request("/inside.js", 200) == b"safe asset"
            assert http_request("/asset.js", 200, "HEAD") == b""
            http_request("/asset.js", 405, "DELETE")
            http_request("/api/ping", 405, "POST")
            http_request("/api/ping-extra", 404)
            http_request("/ws-extra", 404)
            http_request("/api/stats", 403)
            for path in ["/.private", "/%2eprivate", "/%2e%2e/outside.txt", "/asset.js%00", "/bad%zz", "/a%5cb"]:
                http_request(path, 400)
            for path in ["/escape.txt", "/folder/", "/compressed.js"]:
                http_request(path, 403)
            print("PASS: HTTP exact routes, read-only methods, encoded paths, hidden files and symlink confinement")
            login(first, "ADMIN")
            drain(first)
            malformed = [b'{"msg":{"type":"cat"},"cat":{"cmd":"ptt","vfo":"A","ptt":true},}',
                b'{"msg":{"type":"cat"},"msg.type":"auth"}', b'{"a":"trailing\\',
                b'{"a"', b'{"a":1 "b":2}', b'[' * 512 + b'0' + b']' * 512,
                b'{"msg":{"type":"cat"},"cat":{"cmd":"ptt","vfo":"A","ptt":true}}\0junk',
                b'[{"msg":{"type":"cat"},"cat":{"cmd":"ptt","vfo":"A","ptt":true}}]']
            for packet in malformed:
                first.send_payload(packet, 1)
                messages = drain(first)
                assert not any(m.get("cat", {}).get("cmd") == "ptt" for m in messages), messages
            for fields in [{"vfo": ""}, {"vfo": "AA"}, {"vfo": "B"}, {"ptt": "true"}, {"ptt": 1}]:
                cat(first, "ptt", **{"ptt": True, **fields})
                messages = drain(first)
                assert any(m.get("msg", {}).get("type") == "error" for m in messages), fields
                assert not any(m.get("cat", {}).get("cmd") == "ptt" for m in messages)
            for cmd, field, values in [("freq", "freq", [2147483648, 18446744073709551615, 1e300, -1, 1.5]),
                                       ("width", "width", ["3000junk", "-1", "narrowjunk"]),
                                       ("mode", "mode", ["INVALID"]), ("unknown", "unused", ["ignored"])]:
                for value in values:
                    cat(first, cmd, **{field: value})
                    messages = drain(first)
                    assert any(m.get("msg", {}).get("type") == "error" for m in messages), (cmd, value)
                    assert not any(m.get("cat", {}).get("cmd") == cmd for m in messages), (cmd, value)
            cat(first, "width", width="NARR")
            first.until(lambda m: m.get("cat", {}).get("cmd") == "width")
            cat(first, "width", width="2400 Hz")
            first.until(lambda m: m.get("cat", {}).get("cmd") == "width")
            cat(first, "mode", mode="USB")
            acknowledgement = first.until(lambda m: m.get("cat", {}).get("cmd") == "mode")
            assert "state" not in acknowledgement["cat"], acknowledgement
            for cmd, value in [("freq", 14074000), ("mode", "USB"), ("width", "2400"), ("ptt", False)]:
                cat(first, cmd, state={cmd: value})
                messages = drain(first)
                assert any(m.get("msg", {}).get("type") == "error" for m in messages), (cmd, messages)
                assert not any(m.get("cat", {}).get("cmd") == cmd for m in messages), (cmd, messages)
            second = WebSocket(port)
            clients.append(second)
            login(second, "TX")
            drain(second)
            cat(first, "ptt", ptt=True)
            first.until(lambda m: m.get("cat", {}).get("cmd") == "ptt" and m["cat"].get("ptt") is True)
            drain(second)
            cat(second, "ptt", ptt=False)
            messages = drain(second)
            assert any(m.get("msg", {}).get("type") == "error" for m in messages)
            assert not any(m.get("cat", {}).get("cmd") == "ptt" for m in messages)
            cat(second, "ptt", ptt=True)
            messages = drain(second)
            assert any("already transmitting" in m.get("error", {}).get("msg", "") for m in messages)
            cat(first, "ptt", vfo="a", ptt=False)
            first.until(lambda m: m.get("cat", {}).get("cmd") == "ptt" and m["cat"].get("ptt") is False)
            limited = WebSocket(port)
            clients.append(limited)
            login(limited, "LIMITED")
            drain(limited)
            cat(limited, "ptt", ptt=True)
            messages = drain(limited)
            assert any("PTT request rejected" in m.get("error", {}).get("msg", "") for m in messages)
            statuses = [m["talk"]["tx"] for m in messages if m.get("talk", {}).get("cmd") == "userinfo" and m["talk"].get("user") == "LIMITED"]
            assert statuses and statuses[-1] is False
            cat(second, "ptt", ptt=True)
            second.until(lambda m: m.get("cat", {}).get("cmd") == "ptt" and m["cat"].get("user") == "TX" and m["cat"].get("ptt") is True)
            cat(second, "ptt", ptt=False)
            second.until(lambda m: m.get("cat", {}).get("cmd") == "ptt" and m["cat"].get("ptt") is False)
            owner, elmer, noob = (WebSocket(port) for _ in range(3))
            clients.extend([owner, elmer, noob])
            for client, name in [(owner, "OWNER"), (elmer, "ELMER"), (noob, "NOOB")]:
                login(client, name)
                drain(client)
            def key(client):
                drain(client)
                cat(client, "ptt", ptt=True)
                client.until(lambda m: m.get("cat", {}).get("cmd") == "ptt" and m["cat"].get("ptt") is True)
            def stop(client, holder):
                drain(client)
                cat(client, "ptt", ptt=False)
                messages = drain(client)
                assert any(m.get("cat", {}).get("user") == holder and m["cat"].get("ptt") is False for m in messages), messages
                assert not any(m.get("cat", {}).get("ptt") is True for m in messages), messages
            def deny(client, state):
                drain(client)
                cat(client, "ptt", ptt=state)
                messages = drain(client)
                assert any(m.get("msg", {}).get("type") == "error" for m in messages), messages
                assert not any(m.get("cat", {}).get("cmd") == "ptt" for m in messages), messages
            key(owner)
            deny(first, False)  # admin cannot stop owner
            deny(first, True)
            stop(owner, "OWNER")
            key(first)
            deny(owner, True)  # even owner cannot take over by key-down
            stop(owner, "ADMIN")
            key(second)
            stop(first, "TX")
            key(noob)
            stop(second, "NOOB")
            key(noob)
            stop(elmer, "NOOB")
            def quota(kind, command, name, amount=None):
                tail = f"{command} {name}" + (f" {amount}" if amount is not None else "")
                first.send({"msg": {"type": "talk"}, "talk": {"cmd": "quota", "target": kind, "data": tail}})
                return first.until(lambda m: name in m.get("notice", {}).get("msg", "") and "BW used=" in m["notice"]["msg"])
            quota("BW", "SET", "TX", "2")
            with sqlite3.connect(database) as db:
                assert db.execute("SELECT bandwidth_remaining FROM user_usage WHERE name='TX'").fetchone()[0] <= 2000000
            quota("BW", "ADD", "TX", "1G")
            with sqlite3.connect(database) as db:
                assert db.execute("SELECT bandwidth_remaining FROM user_usage WHERE name='TX'").fetchone()[0] > 1000000000
            first.send({"msg": {"type": "talk"}, "talk": {"cmd": "quota", "target": "TX", "data": "SHOW TX"}})
            first.until(lambda m: "TX:" in m.get("notice", {}).get("msg", "") and "remaining" in m["notice"]["msg"])
            first.send({"msg": {"type": "talk"}, "talk": {"cmd": "whois", "target": "TX"}})
            accounting = first.until(lambda m: m.get("talk", {}).get("cmd") == "whois")["talk"]["usage"]
            assert int(accounting["rx-text-frames"]) > 0 and int(accounting["tx-text-bytes"]) > 0
            second.send({"msg": {"type": "talk"}, "talk": {"cmd": "whois", "target": "ADMIN"}})
            assert "usage" not in second.until(lambda m: m.get("talk", {}).get("cmd") == "whois")["talk"]
            drain(second)
            quota("BW", "SET", "TX", "0")
            second.until(lambda m: "Bandwidth allowance exhausted" in m.get("notice", {}).get("msg", ""))
            key(second)  # BW exhaustion is advisory: normal key-down/up remain available.
            stop(second, "TX")
            quota("BW", "RESET", "TX")
            with sqlite3.connect(database) as db:
                before = db.execute("SELECT tx_text_bytes,rx_text_bytes FROM user_usage WHERE name='TX'").fetchone()
                assert sum(before) == 0, before
            second.socket.close()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                with sqlite3.connect(database) as db:
                    closed = db.execute("SELECT details FROM audit_log WHERE event_type='session.usage' AND details LIKE '%user=TX %'").fetchall()
                    row = db.execute("SELECT tx_text_bytes,rx_text_bytes FROM user_usage WHERE name='TX'").fetchone()
                if closed: break
                time.sleep(.05)
            assert len(closed) == 1 and "saved=yes" in closed[0][0] and row is not None
            assert row[1] == 0, row # pre-reset receives must not be charged twice at close
            print("PASS: live TX/BW quotas, SI allowances, private WHOIS usage, reset checkpoints and one end-session AUDIT record")
            print("PASS: strict stop hierarchy, admin/owner boundary and no PTT ownership transfer")
            print("PASS: malformed WebSocket input, strict CAT targets/values and PTT ownership")
            assert process.poll() is None
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

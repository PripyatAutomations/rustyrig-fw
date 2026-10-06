"""Check site login and JOIN/PART media semantics through the production server."""
import base64
import hashlib
import json
import os
import pathlib
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


from ws_helpers import WebSocket


def media_command(message, command):
    return message.get("media", {}).get("cmd") == command


with tempfile.TemporaryDirectory(prefix="rr-rig-rooms-") as temporary:
    work = pathlib.Path(temporary)
    database = work / "master.db"
    with sqlite3.connect(database) as db:
        db.executescript((ROOT / "sql/sqlite.master.sql").read_text())
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(1,?,?,?,?,?)",
            ("TEST", 1, hashlib.sha1(b"test-password").hexdigest(), 3, "admin,view,radio,edit,chat"))
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(2,?,?,?,?,?)",
            ("VIEWER", 1, hashlib.sha1(b"test-password").hexdigest(), 2, "view,chat"))
        db.execute("INSERT INTO users(uid,name,enabled,password,maxsessions,permissions) VALUES(3,?,?,?,?,?)",
            ("OWNER", 1, hashlib.sha1(b"test-password").hexdigest(), 2, "owner,view,chat"))
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    config = work / "rrserver.cfg"
    config.write_text(f"""[general]
station.name=roomtest
rig.instances=rig0 rig1
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
quota.enforce=false
chat.replay-lines=0
codecs.allowed=pc16
[rig:rig0]
backend=internal
vfos=A B
rx-independent-vfos=B
[rig:rig1]
backend=internal
vfos=A
room=#roomtest-rig1
[fwdsp]
path={work}/absent-fwdsp
pcm-hub=false
[callsign-lookup]
enabled=false
""")
    with (work / "console.log").open("w") as output:
        process = subprocess.Popen([str(ROOT / "bin/rrserver"), "-f", str(config)],
            cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        client = None
        try:
            deadline = time.monotonic() + 10
            while client is None:
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError((work / "console.log").read_text())
                try:
                    client = WebSocket(port)
                except ConnectionRefusedError:
                    time.sleep(0.05)
            client.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": "TEST"}})
            challenge = client.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
            first_hash = hashlib.sha1(b"test-password").hexdigest()
            password = hashlib.sha1((first_hash + "+" + challenge["nonce"]).encode()).hexdigest()
            client.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": "TEST",
                "pass": password, "token": challenge["token"]}})
            client.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")
            lobby = client.until(lambda m: m.get("talk", {}).get("cmd") == "join")
            assert lobby["talk"]["room"] == "#roomtest", lobby
            assert lobby["room"]["site"] is True and lobby["room"]["has-vfos"] is False, lobby
            # PTT is operated from the default rig's sole TX room.
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#roomtest-rig0"}})
            client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == "#roomtest-rig0")
            client.send({"msg": {"type": "object"}, "object": {"cmd": "snapshot"},
                "request": {"id": "chat-controls"}})
            client.until(lambda m: m.get("object", {}).get("cmd") == "end")
            # A successful control must not stop parsing the remaining commands.
            for text, expected_mode, expected_frequency in (
                ("!mode lsb", "LSB", None),
                ("!mode usb freq 7200", "USB", 7200000),
                ("!freq 7230 mode lsb", "LSB", 7230000),
                ("!mode usb !freq 7250", "USB", 7250000),
            ):
                client.send({"msg": {"type": "talk"}, "talk": {"cmd": "msg", "msg_type": "pub",
                    "target": "#roomtest-rig0", "data": text}})
                seen = set()
                def changed(message):
                    cat = message.get("cat", {})
                    if cat.get("cmd") == "mode" and cat.get("mode", "").upper() == expected_mode:
                        seen.add("mode")
                    prop = message.get("property", {})
                    if prop.get("name") == "mode" and prop.get("value") == expected_mode:
                        seen.add("observed-mode")
                    if expected_frequency is None or (cat.get("cmd") == "freq" and
                            cat.get("freq") == expected_frequency):
                        seen.add("frequency")
                    return seen == {"mode", "observed-mode", "frequency"}
                client.until(changed)
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "msg", "msg_type": "pub",
                "target": "#roomtest-rig0", "data": "!width narrow freq 7260"}})
            observed = set()
            def width_changed(message):
                prop = message.get("property", {})
                if prop.get("name") == "width" and prop.get("value") == 1800:
                    observed.add("width")
                if message.get("cat", {}).get("freq") == 7260000:
                    observed.add("frequency")
                return observed == {"width", "frequency"}
            client.until(width_changed)
            print("PASS: chat mode/width controls publish observations and stacked commands run in both orders")
            # Internal test rigs have no physical transmitter: verify both PTT status paths.
            for keyed in (True, False):
                client.send({"msg": {"type": "cat"}, "cat": {"cmd": "ptt", "vfo": "A", "ptt": keyed}})
                status = client.until(lambda m: m.get("talk", {}).get("cmd") == "userinfo" and
                    m["talk"].get("user") == "TEST" and m["talk"].get("tx") is keyed)
                if keyed:
                    assert status["talk"]["ptt-vfo"] == "A", status
                echo = client.until(lambda m: m.get("cat", {}).get("cmd") == "ptt")
                assert echo["cat"]["ptt"] is keyed and echo["cat"]["user"] == "TEST", echo
            print("PASS: production PTT on/off userinfo and CAT acknowledgements")
            client.send({"msg": {"type": "media"}, "media": {"cmd": "list"}})
            available = client.until(lambda m: media_command(m, "available") and
                m["media"].get("room") == "#roomtest-rig1")["media"]
            assert available["joined"] is False
            assert available["rig-uuid"] and available["vfo-uuid"]
            uuid = available["chan-uuid"]
            subscribe = {"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": uuid}}
            client.send(subscribe)
            client.until(lambda m: "error" in m)
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#roomtest-rig1"}})
            joined = client.until(lambda m: m.get("talk", {}).get("cmd") == "join")
            assert joined["talk"]["room"] == "#roomtest-rig1" and joined["room"]["vfo-mask"] == 1
            client.until(lambda m: media_command(m, "available") and
                m["media"].get("chan-uuid") == uuid and m["media"].get("joined") is True)
            client.send(subscribe)
            client.until(lambda m: media_command(m, "subscribed") and m["media"]["chan-uuid"] == uuid)
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "part", "target": "#roomtest-rig1"}})
            client.until(lambda m: media_command(m, "unsubscribed") and m["media"]["chan-uuid"] == uuid)
            parted = client.until(lambda m: m.get("talk", {}).get("cmd") == "part")
            assert parted["talk"]["room"] == "#roomtest-rig1"
            client.send(subscribe)
            client.until(lambda m: "error" in m)
            for protected in ("#roomtest", "#roomtest-rig1"):
                client.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": protected + " remove"}})
                client.until(lambda m: "error" in m)
            for protected in ("#roomtest", "#roomtest-rig1"):
                client.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": "remove " + protected + " -f -h"}})
                client.until(lambda m: "error" in m)
            with sqlite3.connect(database) as db:
                assert db.execute("SELECT has_vfos,vfo_mask FROM rooms WHERE name='#roomtest'").fetchone() == (0, 0)
                bindings = db.execute("SELECT binding FROM room_vfos WHERE room='#roomtest-rig1'").fetchall()
                assert bindings == [(available["vfo-uuid"],)], bindings
            def room_command(text):
                client.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": text}})
            def error_contains(fragment):
                error = client.until(lambda m: "error" in m)
                assert fragment.lower() in str(error).lower(), error
            for reserved in ("#roomtest-rig9", "#other-rig123"):
                room_command(reserved + " add")
                error_contains("created only")
                client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": reserved}})
                client.until(lambda m: "error" in m)
            room_command("#roomtest.lounge add")
            client.until(lambda m: "#roomtest.lounge added" in m.get("notice", {}).get("msg", ""))
            room_command("#roomtest.lounge vfo add rig0.vfo_a")
            error_contains("numbered rig rooms")
            rx_room = "#roomtest-rig0.monitor"
            room_command(rx_room + " add")
            client.until(lambda m: rx_room + " added" in m.get("notice", {}).get("msg", ""))
            room_command(rx_room + " vfo add rig1.vfo_a")
            error_contains("must belong")
            for vfo in ("a", "b"):
                room_command(rx_room + " vfo add rig0.vfo_" + vfo)
                client.until(lambda m: rx_room + " VFO " in m.get("notice", {}).get("msg", ""))
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": rx_room}})
            mapped = client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == rx_room)
            assert mapped["room"]["tx-control"] is False and mapped["room"]["vfo-mask"] == 3
            assert mapped["room"]["rx-tuning-mask"] == 2
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "part", "target": "#roomtest-rig0"}})
            client.until(lambda m: m.get("talk", {}).get("cmd") == "part")
            client.send({"msg": {"type": "media"}, "media": {"cmd": "list"}})
            rx_b = client.until(lambda m: media_command(m, "available") and m["media"].get("room") == rx_room and
                m["media"].get("vfo") == 1 and m["media"].get("dir") == 0)["media"]
            assert rx_b["joined"] is True
            client.send({"msg": {"type": "media"}, "media": {"cmd": "list"}})
            base_tx = client.until(lambda m: media_command(m, "available") and m["media"].get("room") == "#roomtest-rig0" and
                m["media"].get("dir") == 1)["media"]
            assert base_tx["joined"] is False
            client.send({"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": base_tx["chan-uuid"]}})
            client.until(lambda m: "error" in m)
            for vfo, expected in (("A", False), ("B", True)):
                client.send({"msg": {"type": "cat"}, "cat": {"cmd": "freq", "room": rx_room, "vfo": vfo, "freq": 145123000}})
                if expected:
                    ack = client.until(lambda m: m.get("cat", {}).get("cmd") == "freq")
                    assert ack["cat"]["room"] == rx_room
                else:
                    error_contains("shared LO")
            client.send({"msg": {"type": "cat"}, "cat": {"cmd": "ptt", "room": rx_room, "vfo": "B", "ptt": True}})
            error_contains("not allowed")
            for name, expected in (("frequency", "ok"), ("mode", "forbidden-room")):
                client.send({"msg": {"type": "property"}, "request": {"id": "rx-policy", "room": rx_room},
                    "target": rx_b["vfo-uuid"], "property": {"cmd": "set", "name": name,
                    "value": 145124000 if name == "frequency" else "FM"}})
                result = client.until(lambda m: m.get("request", {}).get("id") == "rx-policy")
                assert result["result"]["code"] == expected, result
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#roomtest-rig1"}})
            client.until(lambda m: m.get("talk", {}).get("cmd") == "join")
            for state in (True, False):
                client.send({"msg": {"type": "cat"}, "cat": {"cmd": "ptt", "room": "#roomtest-rig1", "vfo": "A", "ptt": state}})
                ack = client.until(lambda m: m.get("cat", {}).get("cmd") == "ptt")
                assert ack["cat"]["room"] == "#roomtest-rig1" and ack["cat"]["ptt"] is state
            # Room removal is confirmed, session/room/options bound, and audited.
            import re
            def confirmation(text):
                room_command(text)
                notice = client.until(lambda m: "To confirm," in m.get("notice", {}).get("msg", ""))["notice"]["msg"]
                assert notice.startswith("To confirm, please use /room remove "), notice
                assert re.fullmatch(r"[0-9a-f]{6}", notice.split()[-1]), notice
                return notice.split("/room ", 1)[1]
            def query(sql, args=()):
                with sqlite3.connect(database) as db:
                    return db.execute(sql, args).fetchall()
            def login_other(name):
                other = WebSocket(port)
                other.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": name}})
                auth = other.until(lambda m: m.get("auth", {}).get("cmd") == "challenge")["auth"]
                digest = hashlib.sha1((first_hash + "+" + auth["nonce"]).encode()).hexdigest()
                other.send({"msg": {"type": "auth"}, "auth": {"cmd": "pass", "user": name,
                    "pass": digest, "token": auth["token"]}})
                other.until(lambda m: m.get("auth", {}).get("cmd") == "authorized")
                other.until(lambda m: m.get("talk", {}).get("cmd") == "join")
                return other
            # RX removal preserves bindings and restoring reapplies the VFO mask.
            approved_rx = confirmation("remove " + rx_room)
            bindings_before = query("SELECT binding FROM room_vfos WHERE room=? ORDER BY binding", (rx_room,))
            room_command(approved_rx)
            client.until(lambda m: rx_room + " removed" in m.get("notice", {}).get("msg", ""))
            assert query("SELECT binding FROM room_vfos WHERE room=? ORDER BY binding", (rx_room,)) == bindings_before
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": rx_room}})
            client.until(lambda m: "error" in m)
            room_command("add " + rx_room)
            client.until(lambda m: rx_room + " added" in m.get("notice", {}).get("msg", ""))
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": rx_room}})
            restored_rx = client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == rx_room)
            assert restored_rx["room"]["vfo-mask"] == 3
            room_command("add #cleanup")
            client.until(lambda m: "#cleanup added" in m.get("notice", {}).get("msg", ""))
            with sqlite3.connect(database) as db:
                db.execute("UPDATE rooms SET topic='keep topic' WHERE name='#cleanup'")
                db.execute("INSERT INTO chat_log(msg_src,msg_dest,msg_type,msg_data) VALUES('TEST','#cleanup','privmsg','keep history')")
            room_command("remove #cleanup -h")
            error_contains("usage")
            approved = confirmation("remove #cleanup")
            assert query("SELECT deleted FROM rooms WHERE name='#cleanup'") == [(0,)]
            assert query("SELECT username FROM audit_log WHERE event_type='room.created' AND details='#cleanup'") == [("TEST",)]
            room_command(approved.replace("#cleanup", "#roomtest.lounge"))
            error_contains("confirmation")
            room_command(approved + " -f")
            error_contains("confirmation")
            room_command("#cleanup remove 0000000")
            error_contains("confirmation")
            peer = login_other("TEST")
            peer.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": approved}})
            assert "confirmation" in str(peer.until(lambda m: "error" in m)).lower()
            peer.socket.close()
            viewer = login_other("VIEWER")
            for text in ("add #forbidden", "remove #cleanup", approved, "remove #cleanup -f -h", "#cleanup remove"):
                viewer.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": text}})
                assert "admin or owner" in str(viewer.until(lambda m: "error" in m)).lower()
            viewer.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#forbidden"}})
            viewer.until(lambda m: "error" in m)
            viewer.socket.close()
            assert not query("SELECT name FROM rooms WHERE name='#forbidden'")
            room_command("#cleanup remove " + approved.split()[-1])
            client.until(lambda m: "#cleanup removed" in m.get("notice", {}).get("msg", ""))
            assert query("SELECT deleted,topic FROM rooms WHERE name='#cleanup'") == [(1, "keep topic")]
            assert query("SELECT username FROM audit_log WHERE event_type='room.removed' AND details='#cleanup'") == [("TEST",)]
            room_command("list")
            listing = client.until(lambda m: m.get("talk", {}).get("cmd") == "room-list")
            assert "#cleanup" not in listing["talk"]["rooms"]
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#CLEANUP"}})
            client.until(lambda m: "error" in m)
            room_command("add #cleanup")
            client.until(lambda m: "#cleanup added" in m.get("notice", {}).get("msg", ""))
            assert query("SELECT deleted,topic FROM rooms WHERE name='#cleanup'") == [(0, "keep topic")]
            room_command(approved)
            error_contains("confirmation")
            approved = confirmation("remove #cleanup -f")
            room_command(approved)
            client.until(lambda m: "#cleanup removed" in m.get("notice", {}).get("msg", ""))
            assert not query("SELECT name FROM rooms WHERE name='#cleanup'")
            assert query("SELECT msg_data FROM chat_log WHERE msg_dest='#cleanup'") == [("keep history",)]
            room_command("add #cleanup")
            client.until(lambda m: "#cleanup added" in m.get("notice", {}).get("msg", ""))
            ptt_count = query("SELECT COUNT(*) FROM ptt_log")
            approved = confirmation("remove #cleanup --force --history")
            room_command(approved)
            client.until(lambda m: "#cleanup removed" in m.get("notice", {}).get("msg", ""))
            assert not query("SELECT msg_data FROM chat_log WHERE msg_dest='#cleanup'")
            assert query("SELECT COUNT(*) FROM ptt_log") == ptt_count
            assert query("SELECT username FROM audit_log WHERE event_type='room.removed' AND details='#cleanup --force --history'") == [("TEST",)]
            owner = login_other("OWNER")
            owner.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": "add #owner-created"}})
            owner.until(lambda m: "#owner-created added" in m.get("notice", {}).get("msg", ""))
            assert query("SELECT username FROM audit_log WHERE event_type='room.created' AND details='#owner-created'") == [("OWNER",)]
            # Account changes take effect immediately, independent of cached flags.
            def owner_privileges(privileges):
                owner.send({"msg": {"type": "talk"}, "talk": {"cmd": "user", "data": "privs VIEWER set " + privileges}})
                owner.until(lambda m: "privileges for VIEWER" in m.get("notice", {}).get("msg", ""))
            owner_privileges("admin,view,chat")
            audited = login_other("VIEWER")
            owner_privileges("view,chat")
            for command in ("mute", "unmute", "kick", "die", "restart", "syslog"):
                audited.send({"msg": {"type": "talk"}, "talk": {"cmd": command, "target": "TEST" if command != "syslog" else "on", "args": {"reason": "audit permission denial"}}})
                audited.until(lambda m: "error" in m)
            audited.send({"msg": {"type": "talk"}, "talk": {"cmd": "msg", "msg_type": "pub", "target": "#roomtest.lounge", "data": "audit forbidden message"}})
            audited.until(lambda m: "error" in m)
            assert not query("SELECT msg_id FROM chat_log WHERE msg_data='audit forbidden message'")
            audited.send({"msg": {"type": "media"}, "media": {"cmd": "subscribe", "chan-uuid": "unknown", "subsys": 1, "dir": 0, "rig": 240, "vfo": 240, "codec": "pc16"}})
            audited.until(lambda m: "creation requires" in m.get("error", {}).get("msg", ""))
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": "#roomtest.lounge"}})
            client.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == "#roomtest.lounge")
            marker = "audit private room message"
            client.send({"msg": {"type": "talk"}, "talk": {"cmd": "msg", "msg_type": "pub", "target": "#roomtest.lounge", "data": marker}})
            client.until(lambda m: m.get("talk", {}).get("data") == marker)
            audited.send({"msg": {"type": "object"}, "object": {"cmd": "inventory"}, "request": {"id": "privacy-barrier"}})
            def privacy_barrier(message):
                assert message.get("talk", {}).get("data") != marker
                return message.get("request", {}).get("id") == "privacy-barrier" and message.get("object", {}).get("cmd") == "inventory-end"
            audited.until(privacy_barrier)
            # Codec permissions follow direction and current account, plus VFO room membership.
            owner_privileges("view,chat,rx")
            audited.send({"msg": {"type": "media"}, "media": {"cmd": "codec", "chan-uuid": rx_b["chan-uuid"], "codec": "pc16"}})
            audited.until(lambda m: "Join room" in m.get("error", {}).get("msg", ""))
            audited.send({"msg": {"type": "talk"}, "talk": {"cmd": "join", "target": rx_room}})
            audited.until(lambda m: m.get("talk", {}).get("cmd") == "join" and m["talk"].get("room") == rx_room)
            audited.send({"msg": {"type": "media"}, "media": {"cmd": "codec", "chan-uuid": rx_b["chan-uuid"], "codec": "pc16"}})
            audited.until(lambda m: m.get("media", {}).get("cmd") == "available" and m["media"].get("chan-uuid") == rx_b["chan-uuid"])
            owner_privileges("view,chat,tx")
            audited.send({"msg": {"type": "media"}, "media": {"cmd": "codec", "chan-uuid": rx_b["chan-uuid"], "codec": "pc16"}})
            audited.until(lambda m: "RX privilege" in m.get("error", {}).get("msg", ""))
            owner_privileges("view,chat")
            audited.send({"msg": {"type": "auth"}, "auth": {"cmd": "login", "user": "OWNER"}})
            audited.until(lambda m: "Already authenticated" in m.get("error", {}).get("msg", ""))
            audited.send({"msg": {"type": "talk"}, "talk": {"cmd": "room", "data": "add #auth-switch-denied"}})
            audited.until(lambda m: "error" in m)
            assert not query("SELECT name FROM rooms WHERE name='#auth-switch-denied'")
            audited.socket.close()
            print("PASS: immediate admin revocation, account codec directions, room message isolation and non-source creation denial")
            owner.socket.close()
            print("PASS: admin/owner room management, option-bound confirmation, soft deletion, restoration, force/history, and attributed audit")
            print("PASS: server-owned rig rooms, RX-only media, same-rig UUID mappings, and per-VFO LO-safe tuning")
            print("PASS: production site login, rig JOIN/PART, scoped media, and persisted UUID bindings")
        except Exception:
            print((work / "console.log").read_text())
            raise
        finally:
            if client:
                client.socket.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    # Invalid configuration must fail before serving users, without replacing bindings.
    original = config.read_text()
    with sqlite3.connect(database) as db:
        before = db.execute("SELECT room,binding FROM room_vfos ORDER BY room,binding").fetchall()
    for invalid in ("#roomtest", "#roomtest-rig0", "#bad room"):
        config.write_text(original.replace("room=#roomtest-rig1", "room=" + invalid))
        rejected = subprocess.run([str(ROOT / "bin/rrserver"), "-f", str(config)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
        assert rejected.returncode != 0, invalid
        assert b"Invalid or duplicate room" in rejected.stdout, rejected.stdout
        with sqlite3.connect(database) as db:
            assert db.execute("SELECT room,binding FROM room_vfos ORDER BY room,binding").fetchall() == before
    print("PASS: invalid/site-colliding/duplicate rig rooms fail with database rollback")

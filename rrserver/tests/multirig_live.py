"""Exercise production registry/backends over real NET rigctl TCP connections."""
import pathlib
import signal
import socket
import sqlite3
import subprocess
import sys
import time

binary, work = sys.argv[1:3]
work = pathlib.Path(work)
external = sys.argv[3] if len(sys.argv) > 3 else None
daemon = None
client = None


def timed_out(signum, frame):
    raise TimeoutError("live multirig validation exceeded 120 seconds")


signal.signal(signal.SIGALRM, timed_out)
signal.alarm(120)


def start_daemon(port):
    proc = subprocess.Popen(
        ["rigctld", "-m", "1", "-o", "-T", "127.0.0.1", "-t", str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        for _ in range(100):
            if proc.poll() is not None:
                raise RuntimeError("test rigctld exited before listening")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return proc
            except OSError:
                time.sleep(0.02)
        raise RuntimeError("test rigctld did not start")
    except BaseException:
        stop(proc)
        raise


def stop(proc):
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def read_until(marker):
    identities = {}
    while True:
        line = client.stdout.readline()
        if not line:
            raise RuntimeError(f"diagnostic exited {client.poll()} before {marker}")
        print(line, end="", flush=True)
        fields = line.split()
        if line.startswith("CONFIG") and not external:
            assert f"model=2 device=127.0.0.1:{port} baud=19200" in line
        if fields and fields[0] == "RIG":
            identities[(fields[1],)] = fields[2]
        elif fields and fields[0] == "VFO":
            identities[(fields[1], fields[2])] = fields[3]
        if line.startswith(marker):
            return identities


def command(value):
    client.stdin.write(value + "\n")
    client.stdin.flush()
    return read_until("PASS: " + value)


try:
    if external:
        config = "config/rrserver.cfg"
        if external != "--configured-endpoint":
            raise ValueError("use --configured-endpoint to inspect the configured endpoint")
    else:
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        daemon = start_daemon(port)
        config = work / "multirig.cfg"
        config.write_text(f"""[general]
rig.instances=rig0 rig1
rig.default=rig0
rig.identity-namespace=live-validation
[rig:rig0]
backend=internal
vfos=A B
hamlib.device=invalid-rig0-endpoint
hamlib.baud=4800
[rig:rig1]
backend=hamlib
vfos=A B
hamlib.model=2
hamlib.device=127.0.0.1:{port}
hamlib.baud=19200
reconnect-interval=1
""")
    previous = None
    for run in range(3 if not external else 2):
        if not external and run == 2:
            config.write_text(config.read_text().replace(
                "reconnect-interval=1", "reconnect-interval=0"))
        client = subprocess.Popen(
            [binary, str(config), str(work / "identity.db")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
        )
        identities = read_until("READY")
        assert len(identities) == 6 and len(set(identities.values())) == 6
        if previous is not None:
            assert identities == previous, "UUIDs changed across process/DB restart"
        previous = identities
        with sqlite3.connect(work / "identity.db") as db:
            for alias, uuid in db.execute("SELECT alias,uuid FROM rig_identities"):
                assert identities[(alias,)] == uuid
            rows = db.execute("""SELECT r.alias,v.config_id,v.uuid
                FROM vfo_identities v JOIN rig_identities r ON r.uuid=v.rig_uuid""").fetchall()
            assert len(rows) == 4
            for alias, native, uuid in rows:
                assert identities[(alias, native)] == uuid
        command("observe" if external else "online")
        if not external and run == 0:
            stop(daemon)
            daemon = None
            command("offline")
            daemon = start_daemon(port)
            command("online")
        if not external and run == 2:
            stop(daemon)
            daemon = None
            command("offline")
            command("offline")
        client.stdin.write("quit\n")
        client.stdin.flush()
        assert client.wait(timeout=15) == 0
        client = None
    if not external:
        # Startup with an unavailable endpoint and retries disabled must also
        # leave the internal default usable.
        client = subprocess.Popen(
            [binary, str(config), str(work / "identity.db")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
        )
        assert read_until("READY") == previous
        command("offline")
        client.stdin.write("quit\n")
        client.stdin.flush()
        assert client.wait(timeout=15) == 0
        client = None
    if external:
        print("PASS: configured registry, default isolation, persistent restart; see VFO snapshots for endpoint observations")
    else:
        print("PASS: real backends, scoped config, canonical observations, default isolation, persistent restart")
finally:
    stop(client)
    stop(daemon)
    signal.alarm(0)

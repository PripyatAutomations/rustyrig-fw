#!/usr/bin/env bash
# Standalone validation of first-install remote-administrator identities.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
import pathlib
import sqlite3
import subprocess
import tempfile

script = "packaging/seed-remote-admin.py"
def run(text, *args):
    return subprocess.run(["python3", script, *args], input=text, text=True,
                          capture_output=True)

for bad in ("", "alice", "alice@", "@camp", "alice@bad/site",
            "admin@camp;DROP", "alice@camp bob", "alice@@camp",
            "alice@camp " * 65, "x" * 4097):
    result = run(bad, "--check")
    assert result.returncode != 0, (bad, result.stdout, result.stderr)

assert run("alice@camp bob@* Alice@CAMP", "--check").returncode == 0
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "master.db"
    connection = sqlite3.connect(path)
    connection.executescript(pathlib.Path("sql/sqlite.master.sql").read_text())
    connection.executescript(pathlib.Path("sql/sqlite.master.preload.sql").read_text())
    connection.close()
    result = run("alice@camp bob@* ALICE@CAMP", str(path))
    assert result.returncode == 0, result.stderr
    with sqlite3.connect(path) as db:
        rows = db.execute(
            "SELECT subject_user,subject_host,privilege,effect,remote_allowed "
            "FROM resource_privileges WHERE granted_by='debian-first-install' "
            "ORDER BY subject_user,privilege").fetchall()
        assert rows == [
            ("alice", "camp", "admin", "allow", 1),
            ("alice", "camp", "remote-admin", "allow", 1),
            ("bob", "*", "admin", "allow", 1),
            ("bob", "*", "remote-admin", "allow", 1),
        ], rows
        assert db.execute("SELECT count(*) FROM users WHERE name='alice'").fetchone() == (0,)
        assert db.execute("SELECT count(*) FROM users WHERE name='bob'").fetchone() == (0,)
print("PASS: remote administrator opt-in requires valid subject names and parameterized grants")
PY

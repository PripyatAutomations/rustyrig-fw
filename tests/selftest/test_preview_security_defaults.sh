#!/usr/bin/env bash
# Fail if development-only cross-site administration slips into a preview seed.
set -euo pipefail
cd "$(dirname "$0")/../.."

python3 - <<'PY'
import pathlib
import sqlite3

connection = sqlite3.connect(":memory:")
connection.executescript(pathlib.Path("sql/sqlite.master.sql").read_text())
connection.executescript(pathlib.Path("sql/sqlite.master.preload.sql").read_text())
admin = connection.execute("SELECT enabled, permissions FROM users WHERE name='admin'").fetchone()
guest = connection.execute("SELECT enabled FROM users WHERE name='guest'").fetchone()
assert admin is not None and admin[0] == 0, admin
assert "remote-admin" not in admin[1].split(","), admin
assert guest == (0,), guest
for table in ("resource_privileges", "privilege_set_assignments"):
    rows = connection.execute(
        f"SELECT * FROM {table} WHERE subject_user='admin' AND subject_host='*'"
    ).fetchall()
    assert not rows, (table, rows)
peers = pathlib.Path("config/rrserver.peers.cfg").read_text()
effective = [
    line.split("=", 1)[1].strip().split(";", 1)[0].strip()
    for line in peers.splitlines()
    if line.strip().startswith("remote-admin.next-hop-only=")
]
assert effective == ["true"], effective
print("PASS: preview seeds deny cross-site admin by default; one-hop policy active")
PY

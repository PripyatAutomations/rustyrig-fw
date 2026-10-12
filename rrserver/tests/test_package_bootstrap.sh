#!/usr/bin/env bash
# Execute the Debian provisioning script against disposable paths and accounts.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
import hashlib
import os
import pathlib
import shutil
import sqlite3
import subprocess
import tempfile

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    commands = root / 'bin'
    commands.mkdir()
    install = shutil.which('install')
    for name in ['addgroup', 'adduser', 'chown', 'setfacl']:
        path = commands / name
        path.write_text('#!/bin/sh\nexit 0\n')
        path.chmod(0o755)
    path = commands / 'install'
    path.write_text('#!/usr/bin/env python3\nimport subprocess, sys\nargs = iter(sys.argv[1:])\nfiltered = []\nfor arg in args:\n    if arg in ["-o", "-g"]: next(args)\n    else: filtered.append(arg)\nsubprocess.run([' + repr(install) + '] + filtered, check=True)\n')
    path.chmod(0o755)
    confmodule = root / 'confmodule'
    confmodule.write_text('db_get() { RET=; case $1 in rustyrig-server/admin-password) RET=${RR_TEST_ADMIN_PASSWORD:-};; rustyrig-server/enable-guest) RET=false;; esac; }\ndb_purge() { :; }\n')
    sql = root / 'var/lib/rustyrig/sql'
    sql.mkdir(parents=True)
    for name in ['sqlite.master.sql', 'sqlite.master.preload.sql']:
        shutil.copy2(pathlib.Path('sql') / name, sql / name)
    permissions = root / 'admin-permissions'
    permissions.write_text(pathlib.Path('packaging/admin-permissions.sh').read_text())
    permissions.chmod(0o755)
    script = root / 'postinst'
    script.write_text(pathlib.Path('debian/rustyrig-server.postinst').read_text()
                      .replace('/var/', str(root / 'var') + '/')
                      .replace('/usr/share/debconf/confmodule', str(confmodule))
                      .replace('/usr/lib/rustyrig/admin-permissions', str(permissions) + ' ' + str(root)))
    env = {**os.environ, 'PATH': str(commands) + ':' + os.environ['PATH'],
           'RR_TEST_ADMIN_PASSWORD': 'disposable-test-password'}
    subprocess.run(['sh', str(script)], env=env, check=True)
    database = root / 'var/lib/rustyrig/db/master.db'
    with sqlite3.connect(database) as connection:
        admin = connection.execute("SELECT enabled,password FROM users WHERE name='admin'").fetchone()
        assert admin == (1, hashlib.sha1(b'disposable-test-password').hexdigest())
        assert connection.execute("SELECT enabled FROM users WHERE name='guest'").fetchone() == (0,)
        connection.execute("UPDATE users SET enabled=0,password='existing-password' WHERE name='admin'")
    env['RR_TEST_ADMIN_PASSWORD'] = 'different-upgrade-password'
    subprocess.run(['sh', str(script)], env=env, check=True)
    with sqlite3.connect(database) as connection:
        assert connection.execute("SELECT enabled,password FROM users WHERE name='admin'").fetchone() == (0, 'existing-password')
    database.unlink()
    env['RR_TEST_ADMIN_PASSWORD'] = ''
    refused = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
    assert refused.returncode != 0 and 'password is required' in refused.stderr
    assert not database.exists()
print('PASS: Debian enables fresh admin, keeps guest disabled, preserves upgrade credentials and rejects empty first-install passwords')
PY

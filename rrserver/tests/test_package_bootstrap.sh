#!/usr/bin/env bash
# Execute the Debian provisioning script against disposable paths and accounts.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
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
    confmodule.write_text('db_get() { RET=; case $1 in rustyrig-server/admin-password) RET=${RR_TEST_ADMIN_PASSWORD:-};; rustyrig-server/enable-guest) RET=${RR_TEST_GUEST_ENABLED:-false};; rustyrig-server/guest-password) RET=${RR_TEST_GUEST_PASSWORD:-};; rustyrig-server/allow-remote-admin) RET=${RR_TEST_REMOTE_ADMIN:-false};; rustyrig-server/remote-admin-subjects) RET=${RR_TEST_REMOTE_SUBJECTS:-};; esac; }\ndb_purge() { :; }\n')
    sql = root / 'var/lib/rustyrig/sql'
    sql.mkdir(parents=True)
    for name in ['sqlite.master.sql', 'sqlite.master.preload.sql']:
        shutil.copy2(pathlib.Path('sql') / name, sql / name)
    permissions = root / 'admin-permissions'
    permissions.write_text(pathlib.Path('packaging/admin-permissions.sh').read_text())
    permissions.chmod(0o755)
    seed_helper = root / 'seed-remote-admin'
    seed_helper.write_text(pathlib.Path('packaging/seed-remote-admin.py').read_text())
    seed_helper.chmod(0o755)
    hash_helper = root / 'rrhashpass'
    hash_helper.write_text("#!/bin/sh\ncat >/dev/null\n[ \"$RR_TEST_HASH_FAIL\" != true ] || exit 1\necho argon2id-fixture\n")
    hash_helper.chmod(0o755)
    script = root / 'postinst'
    script.write_text(pathlib.Path('debian/rustyrig-server.postinst').read_text()
                      .replace('/var/', str(root / 'var') + '/')
                      .replace('/home/rustyrig/SHARED', str(root / 'home/rustyrig/SHARED'))
                      .replace('/usr/share/debconf/confmodule', str(confmodule))
                      .replace('/usr/lib/rustyrig/admin-permissions', str(permissions) + ' ' + str(root))
                      .replace('/usr/lib/rustyrig/rrhashpass', str(hash_helper))
                      .replace('/usr/lib/rustyrig/seed-remote-admin', str(seed_helper)))
    env = {**os.environ, 'PATH': str(commands) + ':' + os.environ['PATH'],
           'RR_TEST_ADMIN_PASSWORD': 'disposable-test-password',
           'RR_TEST_REMOTE_ADMIN': 'true',
           'RR_TEST_REMOTE_SUBJECTS': 'alice@camp bob@*'}
    subprocess.run(['sh', str(script)], env=env, check=True)
    database = root / 'var/lib/rustyrig/db/master.db'
    with sqlite3.connect(database) as connection:
        admin = connection.execute("SELECT enabled,password FROM users WHERE name='admin'").fetchone()
        assert admin == (1, 'argon2id-fixture')
        assert connection.execute("SELECT enabled FROM users WHERE name='guest'").fetchone() == (0,)
        grants = connection.execute(
            "SELECT subject_user,subject_host,privilege FROM resource_privileges "
            "WHERE granted_by='debian-first-install' ORDER BY subject_user,privilege"
        ).fetchall()
        assert grants == [('alice','camp','admin'),('alice','camp','remote-admin'),
                          ('bob','*','admin'),('bob','*','remote-admin')], grants
        assert connection.execute("SELECT count(*) FROM users WHERE name='alice'").fetchone() == (0,)
        connection.execute("UPDATE users SET enabled=0,password='existing-password' WHERE name='admin'")
    env['RR_TEST_ADMIN_PASSWORD'] = 'different-upgrade-password'
    subprocess.run(['sh', str(script)], env=env, check=True)
    with sqlite3.connect(database) as connection:
        assert connection.execute("SELECT enabled,password FROM users WHERE name='admin'").fetchone() == (0, 'existing-password')
        assert connection.execute("SELECT count(*) FROM resource_privileges WHERE granted_by='debian-first-install'").fetchone() == (4,)
    database.unlink()
    env['RR_TEST_HASH_FAIL'] = 'true'
    refused = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
    assert refused.returncode != 0
    assert not database.exists()
    assert not list(database.parent.glob('master.db.new.*'))
    env['RR_TEST_HASH_FAIL'] = 'false'
    env['RR_TEST_ADMIN_PASSWORD'] = ''
    refused = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
    assert refused.returncode != 0 and 'password is required' in refused.stderr
    assert not database.exists()
    # An enabled guest never receives a predictable built-in credential.
    env['RR_TEST_ADMIN_PASSWORD'] = 'new-admin-password'
    env['RR_TEST_GUEST_ENABLED'] = 'true'
    env['RR_TEST_GUEST_PASSWORD'] = ''
    refused = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True)
    assert refused.returncode != 0 and 'guest account requires an explicit password' in refused.stderr
    assert not database.exists()
    env['RR_TEST_GUEST_PASSWORD'] = 'dedicated-guest-secret'
    subprocess.run(['sh', str(script)], env=env, check=True)
    with sqlite3.connect(database) as connection:
        guest = connection.execute("SELECT enabled,password FROM users WHERE name='guest'").fetchone()
        assert guest == (1, 'argon2id-fixture'), guest
print('PASS: Debian only grants opted-in remote identities, requires guest secret and preserves upgrades')
PY

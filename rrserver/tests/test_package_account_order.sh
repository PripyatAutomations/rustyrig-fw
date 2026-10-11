#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 - <<'PY'
from pathlib import Path
rules = Path('debian/rules').read_text()
assert 'install -d -m0770 debian/rustyrig-server/var/lib/rustyrig/db' in rules
assert 'install -d -m0770 debian/rustyrig-server/var/lib/rustyrig/modems' in rules
assert 'install -d -m0770 debian/rustyrig-client/var/lib/rustyrig/db' in rules
assert 'override_dh_installdeb:' in rules
assert 'install -Dm755 debian/$$p.preinst debian/$$p/DEBIAN/preinst' in rules
assert 'install -d -orustyrig ' not in rules
assert 'install -o rustyrig' not in rules
assert Path('debian/rustyrig-server.preinst').exists()
assert Path('debian/rustyrig-client.preinst').exists()
assert Path('debian/rustyrig-fwdsp.preinst').exists()
for package in ('rustyrig-server', 'rustyrig-client', 'rustyrig-fwdsp'):
    preinst = Path(f'debian/{package}.preinst').read_text()
    postinst = Path(f'debian/{package}.postinst').read_text()
    assert 'addgroup --system' in preinst
    assert 'adduser --system' in preinst
    assert 'addgroup --system' not in postinst
    assert 'adduser --system' not in postinst
assert 'rustyrig-modems' in Path('debian/rustyrig-server.preinst').read_text()
assert 'rustyrig-dsp' in Path('debian/rustyrig-fwdsp.preinst').read_text()
fwdsp_post = Path('debian/rustyrig-fwdsp.postinst').read_text()
assert 'adduser rustyrig-dsp rustyrig' in fwdsp_post
spec = Path('packaging/rustyrig.spec').read_text()
assert spec.index('%pre server') < spec.index('%files server')
assert 'groupadd -r rustyrig' in spec and 'useradd -r -g rustyrig' in spec
assert 'chown rustyrig:rustyrig %{buildroot}' not in spec
pkgbuild = Path('packaging/PKGBUILD').read_text()
install_file = Path('packaging/rustyrig.install').read_text()
assert 'install=rustyrig.install' in pkgbuild
assert 'pre_install()' in install_file
assert '_create_service_accounts' in install_file
assert 'groupadd -r rustyrig' in install_file and 'useradd -r -g rustyrig' in install_file
assert 'groupadd -r rustyrig-dsp' in install_file and 'usermod -a -G rustyrig rustyrig-dsp' in install_file
assert 'groupadd -r rustyrig-modems' in install_file
print('PASS: account setup runs in package install hooks, before package files are unpacked')
PY

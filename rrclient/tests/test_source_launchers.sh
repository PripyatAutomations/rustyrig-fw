#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/config" "$work/tools" "$work/bin"
touch "$work/librustyaxe.so.0" "$work/librrprotocol.so.0" "$work/libfwdspmgr.so.0"
cp tools/test-env.sh tools/test-config.py tools/test-client.sh tools/test-server.sh "$work/tools/"
cp config/rrclient.cfg config/rrserver.cfg config/callsign-lookup.cli.cfg config/callsign-lookup.srv.cfg "$work/config/"
for program in rrclient rrserver; do
  cat > "$work/bin/$program" <<'MOCK'
#!/bin/bash
set -eu
[[ $1 == -f && $2 == "$PWD/run/"*.cfg ]]
[[ $LD_LIBRARY_PATH == "$PWD"* ]]
[[ $PATH == "$PWD/bin:$PWD/tools:"* ]]
python3 - "$PWD" "$2" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for config in (root/'run').glob('*.cfg'):
    for line in config.read_text().splitlines():
        if line.lstrip().startswith((';', '#', '//')):
            continue
        assert not any(p in line for p in ('/var/lib/rustyrig', '/var/log/', '/etc/rustyrig', '/usr/lib/rustyrig', '/usr/bin/callsign-lookup', '~/.config/')), line
assert str(root/'bin/fwdsp') in pathlib.Path(sys.argv[2]).read_text()
PY
MOCK
  chmod +x "$work/bin/$program"
done
(cd /tmp && bash "$work/tools/test-client.sh")
(cd /tmp && bash "$work/tools/test-server.sh")
rm "$work/librrprotocol.so.0"
if (cd /tmp && bash "$work/tools/test-client.sh" 2>/dev/null); then
  echo 'FAIL: launcher accepted a missing local library' >&2
  exit 1
fi
echo 'PASS: launchers from another working directory select local libraries, executables, configs, databases and logs'

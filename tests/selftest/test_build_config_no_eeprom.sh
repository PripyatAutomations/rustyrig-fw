#!/usr/bin/env bash
# Native profiles must not need to pack EEPROM images to build.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 tools/gen-build-config.py radio --mongoose false --output "$tmp/native.h"
grep -q '^#define USE_RUSTYIRC 1$' "$tmp/native.h"
grep -q '^#define HOST_POSIX$' "$tmp/native.h"
! grep -q '^#define USE_MONGOOSE' "$tmp/native.h"
! grep -q '^#define USE_EEPROM' "$tmp/native.h"
test ! -e "$tmp/eeprom.bin"
test ! -e "$tmp/eeprom_types.h"
test ! -e "$tmp/eeprom_layout.h"

python3 tools/gen-build-config.py radio --mongoose true --output "$tmp/mongoose.h"
grep -q '^#define USE_MONGOOSE$' "$tmp/mongoose.h"
test ! -e "$tmp/eeprom.bin"

# Missing eeprom settings should not break the native header generator.
python3 - "$tmp" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1])
cfg = json.loads(pathlib.Path("config/radio.config.json").read_text())
cfg.pop("eeprom", None)
pathlib.Path("config/zz-build-config-fixture.config.json").write_text(json.dumps(cfg))
PY
trap 'rm -rf "$tmp"; rm -f config/zz-build-config-fixture.config.json' EXIT
python3 tools/gen-build-config.py zz-build-config-fixture --mongoose false --output "$tmp/no-eeprom.h"
! grep -q '^#define EEPROM_SIZE' "$tmp/no-eeprom.h"
echo 'PASS: native build headers do not require EEPROM generation'

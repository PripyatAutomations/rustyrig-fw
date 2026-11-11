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
# Filters and ATU tables are application configuration, not EEPROM payloads.
test -f "$tmp/filter_tables.h"
test -f "$tmp/atu_tables.h"
grep -q '^#define MAX_TUNERS 1
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
(p / "native.json").write_text(json.dumps(cfg))
PY
python3 tools/gen-build-config.py radio --config "$tmp/native.json" --mongoose false --output "$tmp/no-eeprom.h"
! grep -q '^#define EEPROM_SIZE' "$tmp/no-eeprom.h"
echo 'PASS: native build, filters, and ATU headers generate without EEPROM image'
 "$tmp/atu_tables.h"
grep -q '642.0f' "$tmp/atu_tables.h"
cat >"$tmp/header-smoke.c" <<'C'
#define FILTERS_C
#define ANT_TUNER
#include "filter_tables.h"
#include "atu_tables.h"
int main(void) {
   return atu_table_c[0][0] > 0 && atu_table_l[0][0] > 0 ? 0 : 1;
}
C
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$tmp" -fsyntax-only "$tmp/header-smoke.c"
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
(p / "native.json").write_text(json.dumps(cfg))
PY
python3 tools/gen-build-config.py radio --config "$tmp/native.json" --mongoose false --output "$tmp/no-eeprom.h"
! grep -q '^#define EEPROM_SIZE' "$tmp/no-eeprom.h"
echo 'PASS: native build headers do not require EEPROM generation'

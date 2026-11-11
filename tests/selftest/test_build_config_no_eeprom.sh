#!/usr/bin/env bash
# Native profiles must generate filter/ATU tables without packing EEPROM.
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 tools/gen-build-config.py radio --mongoose false --output "$tmp/build_config.h"
grep -q '^#define USE_RUSTYIRC 1$' "$tmp/build_config.h"
grep -q '^#define HOST_POSIX$' "$tmp/build_config.h"
! grep -q '^#define USE_MONGOOSE' "$tmp/build_config.h"
! grep -q '^#define USE_EEPROM' "$tmp/build_config.h"

test -f "$tmp/filter_tables.h"
test -f "$tmp/atu_tables.h"
grep -q '^#define MAX_TUNERS 1$' "$tmp/atu_tables.h"
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
cc -std=c11 -Wall -Wextra -Werror -I"$tmp" -fsyntax-only "$tmp/header-smoke.c"

test ! -e "$tmp/eeprom.bin"
test ! -e "$tmp/eeprom_types.h"
test ! -e "$tmp/eeprom_layout.h"

python3 tools/gen-build-config.py radio --mongoose true --output "$tmp/build_config.h"
grep -q '^#define USE_MONGOOSE$' "$tmp/build_config.h"

# Missing EEPROM configuration must not break native header generation.
python3 - "$tmp" <<'PY'
import json
import pathlib
import sys
p = pathlib.Path(sys.argv[1])
cfg = json.loads(pathlib.Path("config/radio.config.json").read_text())
cfg.pop("eeprom", None)
(p / "no-eeprom.json").write_text(json.dumps(cfg))
PY
python3 tools/gen-build-config.py radio --config "$tmp/no-eeprom.json" --mongoose false --output "$tmp/build_config.h"
! grep -q '^#define EEPROM_SIZE' "$tmp/build_config.h"
test -f "$tmp/filter_tables.h"
test -f "$tmp/atu_tables.h"
echo 'PASS: native config, filter, and ATU headers generated without EEPROM'

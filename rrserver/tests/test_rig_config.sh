#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/multirig.cfg" <<'CFG'
[general]
rig.instances=first second
rig.default=first
[rig:first]
backend=internal
vfos=A B
[rig:second]
backend=hamlib
vfos=A
hamlib.device=localhost:4533
hamlib.baud=9600
CFG
${CC:-cc} -std=gnu11 -I. -Iinc -Ibuild/${PROFILE:-radio} \
   $(pkg-config --cflags glib-2.0) \
   rrserver/tests/rig_config.c rrserver/cfg.rig.c rrserver/defconfig.c \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   $(pkg-config --libs glib-2.0) -o "$work/rig_config"
"$work/rig_config" "$work/multirig.cfg"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -I. -I"build/${PROFILE:-radio}" $(pkg-config --cflags glib-2.0 openssl) \
   rrserver/tests/mesh_probe.c rrserver/identity.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe $(pkg-config --libs glib-2.0 openssl sqlite3) -o build/mesh-probe
python3 rrserver/tests/mesh_native_live.py

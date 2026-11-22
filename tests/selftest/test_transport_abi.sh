#!/usr/bin/env bash
# A split librustyaxe/librrprotocol installation must share one rrconn layout
# regardless of whether RustyRig is built with the optional Mongoose backend.
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/layout.c" <<'C'
#include <stddef.h>
#include <stdio.h>
#include <librustyaxe/core.h>
int main(void) {
   printf("%zu %zu %zu %zu\n", sizeof(rrconn_t),
      offsetof(rrconn_t, ghost_time), offsetof(rrconn_t, rooms),
      offsetof(rrconn_t, rx_packets));
   return 0;
}
C
for transport in native mongoose; do
   flags=()
   [[ $transport == native ]] || flags+=(-DUSE_MONGOOSE=1)
   ${CC:-cc} -std=gnu11 "${flags[@]}" $(pkg-config --cflags librustyaxe) \
      "$work/layout.c" -o "$work/$transport"
   "$work/$transport" > "$work/$transport.layout"
done
if ! cmp -s "$work/native.layout" "$work/mongoose.layout"; then
   echo "FAIL: installed librustyaxe rrconn_t ABI differs between transport profiles." >&2
   echo "Rebuild and reinstall librustyaxe/librrprotocol with unconditional legacy pointer slots." >&2
   cat "$work/"*.layout >&2
   exit 1
fi
echo "PASS: rrconn_t layout is transport-independent across separately built libraries"

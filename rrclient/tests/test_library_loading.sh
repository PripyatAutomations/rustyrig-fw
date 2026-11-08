#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

make -s -C "$root" bin/rrclient install-libs DESTDIR="$work/prefix"
mkdir -p "$work/prefix/bin" "$work/outside"
cp "$root/bin/rrclient" "$work/prefix/bin/"

for binary in "$root/bin/rrclient" "$work/prefix/bin/rrclient"; do
   if [[ "$binary" == "$root/bin/rrclient" ]]; then
      library_dir="$root"
   else
      library_dir="$(dirname "$(find "$work/prefix" -name librrprotocol.so.0 -print -quit)")"
      [[ -n "$library_dir" ]]
   fi
   (
      cd "$work/outside"
      # Bind every symbol, including helpers not called by --help.
      env -u LD_PRELOAD LD_LIBRARY_PATH="$library_dir" LD_BIND_NOW=1 "$binary" -h > "$work/help"
      env -u LD_PRELOAD LD_LIBRARY_PATH="$library_dir" ldd "$binary" > "$work/ldd"
   )
   for library in librrprotocol libfwdspmgr; do
      resolved="$(awk -v name="$library.so.0" '$1 == name {print $3}' "$work/ldd")"
      [[ "$(readlink -f "$resolved")" == "$(readlink -f "$library_dir/$library.so.0")" ]]
   done
   axe_resolved="$(awk '$1 == "librustyaxe.so.0" {print $3}' "$work/ldd")"
   [[ -n "$axe_resolved" && -f "$axe_resolved" ]]
done

echo "PASS: source and installed clients load matching libraries outside their working directory"

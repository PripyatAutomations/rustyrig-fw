#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

make -s -C "$root" bin/rrclient install-libs INSTALL_DIR="$work/prefix"
mkdir -p "$work/prefix/bin" "$work/outside"
cp "$root/bin/rrclient" "$work/prefix/bin/"

for binary in "$root/bin/rrclient" "$work/prefix/bin/rrclient"; do
   if [[ "$binary" == "$root/bin/rrclient" ]]; then
      library_dir="$root"
   else
      library_dir="$work/prefix/lib"
   fi
   (
      cd "$work/outside"
      # Bind every symbol, including helpers not called by --help.
      env -u LD_LIBRARY_PATH -u LD_PRELOAD LD_BIND_NOW=1 "$binary" -h > "$work/help"
      env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$binary" > "$work/ldd"
   )
   for library in librustyaxe librrprotocol libfwdspmgr; do
      resolved="$(awk -v name="$library.so.0" '$1 == name {print $3}' "$work/ldd")"
      [[ "$(readlink -f "$resolved")" == "$(readlink -f "$library_dir/$library.so.0")" ]]
   done
done

echo "PASS: source and installed clients load matching libraries outside their working directory"

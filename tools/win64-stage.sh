#!/usr/bin/env bash
# Experimental MSYS2 UCRT64 staging helper. Invoke with bash; no POSIX host
# or Linux installation paths are changed. Packaging is not a build test.
set -euo pipefail
if [[ "${MSYSTEM:-}" != UCRT64 ]]; then
  echo "Run in an MSYS2 UCRT64 shell (MSYSTEM=UCRT64)." >&2
  exit 1
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${MINGW_PREFIX:-/ucrt64}
stage=${1:-"$root/build/win64-stage"}
[[ "$stage" = /* ]] || stage="$PWD/$stage"
for tool in objdump awk cp find; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
[[ -f "$root/bin/rrclient.exe" ]] || { echo "Missing bin/rrclient.exe: build the Windows target first." >&2; exit 1; }
[[ -f "$root/bin/rrclient-gtk.dll" ]] || { echo "Missing bin/rrclient-gtk.dll: GTK frontend DLL has not been linked." >&2; exit 1; }
[[ -d "$prefix/bin" ]] || { echo "Missing MSYS2 runtime $prefix/bin" >&2; exit 1; }
mkdir -p "$stage/bin" "$stage/lib" "$stage/share" "$stage/config"
cp -f "$root/bin/rrclient.exe" "$root/bin/rrclient-gtk.dll" "$stage/bin/"
# Copy locally built RustyRig DLLs (including librrprotocol and librustyaxe).
shopt -s nullglob
project_dlls=("$root"/*.dll "$root/bin"/*.dll)
for dll in "${project_dlls[@]}"; do cp -f "$dll" "$stage/bin/"; done
# GTK/GStreamer need plugin modules and share data, not only the linked DLLs.
for dir in gstreamer-1.0 gio/modules gdk-pixbuf-2.0 gtk-3.0; do
  if [[ -d "$prefix/lib/$dir" ]]; then
    mkdir -p "$stage/lib/$dir"
    cp -a "$prefix/lib/$dir/." "$stage/lib/$dir/"
  fi
done
for dir in glib-2.0/schemas icons/hicolor icons/Adwaita themes; do
  if [[ -d "$prefix/share/$dir" ]]; then
    mkdir -p "$stage/share/$dir"
    cp -a "$prefix/share/$dir/." "$stage/share/$dir/"
  fi
done
# Avoid the Linux paths in the sample config. No installed config is replaced.
sed -e 's@^path.modules=.*@path.modules=./bin@' \
    -e 's@^path.help-dir=.*@path.help-dir=./share/rustyrig/help@' \
    -e 's@rrclient-gtk\.so@rrclient-gtk.dll@g' \
    "$root/config/rrclient.cfg" > "$stage/config/rrclient.cfg.example"
# The config uses a relative module directory. Launch from the installation
# root, regardless of the Start Menu / Explorer working directory.
printf '@echo off\r\ncd /d "%%~dp0"\r\n"%%~dp0bin\\rrclient.exe" %%*\r\n' > "$stage/RustyRig.cmd"
# Resolve MinGW import dependencies for the executable, local DLLs and plugins.
# Fail loudly on unknown missing imports instead of silently shipping a broken
# archive. Windows system libraries are supplied by the OS.
declare -A seen=()
queue=()
while IFS= read -r -d '' file; do queue+=("$file"); done < <(find "$stage/bin" "$stage/lib" -type f \( -iname '*.dll' -o -iname '*.exe' \) -print0)
for (( i=0; i<${#queue[@]}; i++ )); do
  binary=${queue[i]}
  while IFS= read -r dep; do
    [[ -n "$dep" ]] || continue
    key=${dep,,}
    [[ "${seen[$key]:-}" ]] && continue
    seen[$key]=1
    [[ -f "$stage/bin/$dep" ]] && continue
    # Case-insensitive resolution for MinGW imports.
    found=$(find "$prefix/bin" -maxdepth 1 -type f -iname "$dep" -print -quit)
    if [[ -n "$found" ]]; then
      cp -f "$found" "$stage/bin/"
      queue+=("$stage/bin/${found##*/}")
      continue
    fi
    case "$key" in
      api-ms-win-*|ext-ms-*|kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|ws2_32.dll|ntdll.dll|msvcrt.dll|ucrtbase.dll|comdlg32.dll|comctl32.dll|rpcrt4.dll|crypt32.dll|bcrypt.dll|secur32.dll|shlwapi.dll|version.dll|winmm.dll|imm32.dll|dwmapi.dll|setupapi.dll|iphlpapi.dll|dnsapi.dll|dbghelp.dll|psapi.dll|winspool.drv|msimg32.dll|netapi32.dll|userenv.dll|wtsapi32.dll|hid.dll|cfgmgr32.dll|normaliz.dll|usp10.dll|opengl32.dll|glu32.dll|d3d11.dll|dxgi.dll|dxguid.dll|dwrite.dll|d2d1.dll|propsys.dll|winhttp.dll|wininet.dll|wldap32.dll|wsock32.dll|imagehlp.dll|powrprof.dll|mpr.dll|avrt.dll|mfplat.dll|mf.dll|mfreadwrite.dll|mfuuid.dll|strmiids.dll|quartz.dll) ;;
      *) echo "Unresolved DLL import: $dep (required by $binary)" >&2; exit 1 ;;
    esac
  done < <(objdump -p "$binary" 2>/dev/null | awk '/DLL Name:/{print $3}')
done
echo "Staged Win64 distribution: $stage"
echo "Experimental packaging only: Windows build and runtime remain unverified."

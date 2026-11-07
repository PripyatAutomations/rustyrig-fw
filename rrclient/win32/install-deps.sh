#!/usr/bin/env bash
# Run from MSYS2 UCRT64; do not launch pacman from PowerShell.
set -euo pipefail
[[ "${MSYSTEM:-}" == UCRT64 ]] || {
  echo "Open the MSYS2 UCRT64 shell first." >&2
  exit 1
}
prefix=mingw-w64-ucrt-x86_64
pacman -S --needed -- \
  base-devel git make jq perl python pkgconf nsis \
  "${prefix}-toolchain" "${prefix}-pkgconf" \
  "${prefix}-gtk3" "${prefix}-gstreamer" \
  "${prefix}-gst-plugins-base" "${prefix}-gst-plugins-good" \
  "${prefix}-gst-plugins-bad" "${prefix}-gst-libav" \
  "${prefix}-sqlite3" "${prefix}-curl" \
  "${prefix}-openssl" "${prefix}-mbedtls" \
  "${prefix}-libogg" "${prefix}-libvorbis" \
  "${prefix}-opus" "${prefix}-flac" \
  "${prefix}-libsndfile" "${prefix}-libsamplerate"
echo "UCRT64 build dependencies installed. This does not mean RustyRig builds on Windows yet."

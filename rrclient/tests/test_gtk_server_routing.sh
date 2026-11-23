#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
if [[ ${RR_TEST_XVFB:-} != 1 ]]; then
   exec "$(dirname "$0")/../../tests/with-xvfb.sh" bash "$0" "$@"
fi
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists gtk+-3.0 librustyaxe librrprotocol; then
   echo 'SKIP: GTK server routing test requires installed GTK, librustyaxe and librrprotocol SDKs'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu23 -DUSE_GTK=1 -Wno-deprecated-declarations \
   -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections \
   $(pkg-config --cflags gtk+-3.0 librustyaxe librrprotocol) \
   rrclient/tests/gtk_server_routing.c -Wl,--gc-sections \
   $(pkg-config --libs gtk+-3.0 librustyaxe librrprotocol) \
   -o "$work/gtk_server_routing"
"$work/gtk_server_routing"

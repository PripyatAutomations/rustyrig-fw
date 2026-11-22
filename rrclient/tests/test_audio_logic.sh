#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/../.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -std=gnu23 -Wall -Wextra -Werror -Iinc rrclient/audio.logic.c rrclient/tests/audio_logic.c -o "$bin"
"$bin"

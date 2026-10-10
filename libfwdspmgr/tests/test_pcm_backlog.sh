#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. libfwdspmgr/tests/pcm_backlog.c -o "$work/pcm_backlog"
"$work/pcm_backlog"

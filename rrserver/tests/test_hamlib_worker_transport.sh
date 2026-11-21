#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# Both targets are hardware-free: protocol framing plus a real fake child.
make --no-print-directory PROFILE="${PROFILE:-radio}" test-hamlib-ipc test-hamlib-transport
echo 'PASS: Hamlib IPC framing and private worker transport'

#!/usr/bin/env bash
# The Mongoose transport no longer exists. Native plain TCP/TLS IRC coverage
# is maintained by test_irc_live.sh using librustyaxe's TCP driver.
set -euo pipefail
echo 'SKIP: removed Mongoose backend (native IRC TCP/TLS: test_irc_live.sh)'

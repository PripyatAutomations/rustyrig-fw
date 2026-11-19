#!/usr/bin/env bash
# Mongoose was removed from RustyRig's IRC transport. Do not try to compile
# obsolete mg_connection fixtures against the native OpenSSL backend.
# Native socket/TLS behavior is exercised by test_irc_live.sh.
set -euo pipefail
echo 'SKIP: removed Mongoose transport (native IRC coverage: test_irc_live.sh)'

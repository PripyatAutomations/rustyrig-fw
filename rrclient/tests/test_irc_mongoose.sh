#!/usr/bin/env bash
# Mongoose was removed from RustyRig's IRC transport. Do not try to compile
# obsolete mg_connection fixtures against the native OpenSSL backend.
# Native socket behavior is covered by IRC parser/queue tests. The separate
# IRC live TLS fixture still needs migration off Mongoose.
set -euo pipefail
echo 'SKIP: removed Mongoose backend; native IRC TLS fixture migration remains open'

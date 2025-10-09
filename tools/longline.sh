#!/bin/sh
WANT_BYTES=$1
[ -z "$WANT_BYTES" ] && WANT_BYTES=256

< /dev/urandom tr -dc 'A-Za-z0-9' | head -c ${WANT_BYTES}; echo


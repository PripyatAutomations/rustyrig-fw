#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/input.cfg" <<'CONFIG'
[acl:private]
deny=guest@10.200.0.0/16
allow=10.0.0.0/8
deny=all
[acl:staff]
allow=@admin
[listener:http]
acl=private
[listener:link]
acl=staff
[peer]
enabled=false
acl=private
[peer:branch]
enabled=false
acl=staff
[rtp]
enabled=false
ports=8422-8499
acl=private
[rtp:staff]
enabled=true
ports=9000-9010
acl=staff
CONFIG
${CC:-cc} -std=gnu11 -I. -Ibuild/${PROFILE:-radio} rrserver/tests/acl_config.c \
   rrserver/acl.c rrserver/cfg.peer.c rrserver/cfg.rtp.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/acl-config"
"$work/acl-config" "$work/input.cfg" "$work/saved.cfg"

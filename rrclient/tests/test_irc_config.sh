#!/usr/bin/env bash
# Old cfg.servers.c was removed when librrprotocol became independent.
# The maintained transport URL parser test covers current IRC/IRCS URLs.
set -euo pipefail
echo 'SKIP: obsolete embedded cfg.servers.c fixture (see test_transport_urls.sh)'

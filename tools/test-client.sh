#!/bin/bash
G_DEBUG=fatal-warnings

set -e
source "$(dirname -- "${BASH_SOURCE[0]}")/test-env.sh"
rr_test_config=$(python3 tools/test-config.py "$rr_test_root" rrclient)
case "${1:-}" in
    gtk)
       G_DEBUG=fatal-criticals
       ;;
    gdb)
       exec gdb --ex run --args "$rr_test_root/bin/rrclient" -f "$rr_test_config"
       ;;
    valgrind)
       exec valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --log-file="$rr_test_root/audit-logs/valgrind.rrclient.%p.log" "$rr_test_root/bin/rrclient" -f "$rr_test_config"
       ;;
    *)
       exec "$rr_test_root/bin/rrclient" -f "$rr_test_config"
       ;;
esac

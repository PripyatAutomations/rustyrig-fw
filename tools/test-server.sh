#!/bin/bash
set -e
source "$(dirname -- "${BASH_SOURCE[0]}")/test-env.sh"
[ -x bin/rrserver ] || ./build.sh
rr_test_config=$(python3 tools/test-config.py "$rr_test_root" rrserver)
case "${1:-}" in
    gdb) exec gdb --ex run --args "$rr_test_root/bin/rrserver" -f "$rr_test_config" ;;
    valgrind) exec valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --log-file="$rr_test_root/audit-logs/valgrind.rrserver.%p.log" "$rr_test_root/bin/rrserver" -f "$rr_test_config" ;;
    *) exec "$rr_test_root/bin/rrserver" -f "$rr_test_config" ;;
esac

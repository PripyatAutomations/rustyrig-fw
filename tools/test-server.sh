#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/test-env.sh"
source "$rr_test_root/tools/test-sanitizers.sh"

sanitizer=plain
media_san=false
debugger=
for option in "$@"; do
    case "$option" in
        media-san) media_san=true ;;
        asan|ubsan|san)
            if [[ "$sanitizer" != plain && "$sanitizer" != "$option" ]]; then
                echo "Choose one of asan, ubsan or san (combined)." >&2; exit 2
            fi
            sanitizer=$option ;;
        gdb|valgrind)
            if [[ -n "$debugger" && "$debugger" != "$option" ]]; then
                echo "Choose either gdb or valgrind." >&2; exit 2
            fi
            debugger=$option ;;
        -h|--help|help)
            echo "Usage: $0 [gdb|valgrind] [asan|ubsan|san] [media-san]"
            echo "san runs both sanitizers; switching modes rebuilds the active source tree."
            exit 0 ;;
        *)
            echo "Unknown test-server option: $option" >&2
            exit 2 ;;
    esac
done
if [[ "$sanitizer" != plain && "$debugger" == valgrind ]]; then
    echo "Valgrind and sanitizer instrumentation cannot be combined." >&2; exit 2
fi
rr_test_prepare_build "$sanitizer" "$media_san"
rr_test_config=$(python3 tools/test-config.py "$rr_test_root" rrserver)
server_args=(-f "$rr_test_config")
case "$debugger" in
    gdb) exec gdb --ex run --args "$rr_test_root/bin/rrserver" "${server_args[@]}" ;;
    valgrind)
        exec valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all \
            --log-file="$rr_test_root/audit-logs/valgrind.rrserver.%p.log" \
            "$rr_test_root/bin/rrserver" "${server_args[@]}" ;;
    *) exec "$rr_test_root/bin/rrserver" "${server_args[@]}" ;;
esac

#!/bin/bash
set -e
source "$(dirname -- "${BASH_SOURCE[0]}")/test-env.sh"
rr_test_config=$(python3 tools/test-config.py "$rr_test_root" rrclient)

# Options are independent of order: gtk gdb == gdb gtk.
# GTK is the normal configured frontend; -T explicitly selects TUI.
mode=gtk
debugger=
for option in "$@"; do
    case "$option" in
        gtk|tui) mode=$option ;;
        gdb|valgrind)
            if [ -n "$debugger" ] && [ "$debugger" != "$option" ]; then
                echo "Choose either gdb or valgrind, not both." >&2
                exit 2
            fi
            debugger=$option
            ;;
        -h|--help|help)
            echo "Usage: $0 [gtk|tui] [gdb|valgrind]"
            echo "Options may be supplied in any order (gtk is the default)."
            exit 0
            ;;
        *)
            echo "Unknown test-client option: $option" >&2
            echo "Usage: $0 [gtk|tui] [gdb|valgrind]" >&2
            exit 2
            ;;
    esac
done

client_args=(-f "$rr_test_config")
if [ "$mode" = tui ]; then
    client_args+=(-T)
fi

case "$debugger" in
    gdb)
        G_DEBUG=fatal-criticals exec gdb --ex run --args "$rr_test_root/bin/rrclient" "${client_args[@]}"
        ;;
    valgrind)
        exec valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all \
            --log-file="$rr_test_root/audit-logs/valgrind.rrclient.%p.log" \
            "$rr_test_root/bin/rrclient" "${client_args[@]}"
        ;;
    *)
        exec "$rr_test_root/bin/rrclient" "${client_args[@]}"
        ;;
esac

#!/bin/bash
set -e
source "$(dirname -- "${BASH_SOURCE[0]}")/test-env.sh"
source "$rr_test_root/tools/test-sanitizers.sh"

# Options are independent of order: gtk gdb == gdb gtk.
# GTK is the normal configured frontend; -T explicitly selects TUI.
mode=gtk
fatal_gtk_warnings=false
debugger=
sanitizer=plain
media_san=false
for option in "$@"; do
    case "$option" in
        gtk) mode=gtk; fatal_gtk_warnings=true ;;
        tui) mode=tui ;;
        media-san) media_san=true ;;
        asan|ubsan|san)
            if [[ "$sanitizer" != plain && "$sanitizer" != "$option" ]]; then
                echo "Choose one of asan, ubsan or san (combined)." >&2; exit 2
            fi
            sanitizer=$option ;;
        gdb|valgrind)
            if [ -n "$debugger" ] && [ "$debugger" != "$option" ]; then
                echo "Choose either gdb or valgrind, not both." >&2
                exit 2
            fi
            debugger=$option
            ;;
        -h|--help|help)
            echo "Usage: $0 [gtk|tui] [gdb|valgrind] [asan|ubsan|san] [media-san]"
            echo "Options may be supplied in any order. GTK is the default frontend;"
            echo "explicit gtk makes GTK/GLib warnings fatal; gdb alone does not."
            echo "san runs both sanitizers; sanitizer mode changes trigger a full rebuild."
            exit 0
            ;;
        *)
            echo "Unknown test-client option: $option" >&2
            echo "Usage: $0 [gtk|tui] [gdb|valgrind] [asan|ubsan|san]" >&2
            exit 2
            ;;
    esac
done

if [[ "$sanitizer" != plain && "$debugger" == valgrind ]]; then
    echo "Valgrind and sanitizer instrumentation cannot be combined." >&2; exit 2
fi
rr_test_prepare_build "$sanitizer" "$media_san"
# rrclient itself is linked with the requested sanitizer runtime. Preloading
# libasan into *every* descendant is unnecessary and can break GTK's glycin
# bwrap image-loader helper (which executes in a restricted sandbox). Keep
# sanitizer instrumentation active in rrclient, but do not inject its runtime
# into external child processes.
if [[ "$sanitizer" == asan || "$sanitizer" == san ]]; then
    unset LD_PRELOAD
fi
# LeakSanitizer cannot run its exit check while traced by GDB.
# Keep ASan/UBSan active; only disable LSan in this combination.
if [[ "$debugger" == gdb && ("$sanitizer" == asan || "$sanitizer" == san) ]]; then
    export ASAN_OPTIONS="${ASAN_OPTIONS:-halt_on_error=1:abort_on_error=1}:detect_leaks=0"
fi

rr_test_config=$(python3 tools/test-config.py "$rr_test_root" rrclient)

if [ "$fatal_gtk_warnings" = true ]; then
    export G_DEBUG=fatal-warnings
fi

client_args=(-f "$rr_test_config")
if [ "$mode" = tui ]; then
    client_args+=(-T)
fi

case "$debugger" in
    gdb)
        exec gdb --ex "handle SIGPIPE nostop noprint pass" --ex run --args "$rr_test_root/bin/rrclient" "${client_args[@]}"
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

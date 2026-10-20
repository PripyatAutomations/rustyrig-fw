#!/usr/bin/env bash
# Shared build-mode handling for source-tree rrclient/rrserver QC launchers.
# SANITIZE is a make-time override; a mode change forces a full rebuild because
# Make does not track command-line compiler flag changes.
rr_test_prepare_build() {
    local mode=$1 media=${2:-false} flags='' stamp="$rr_test_root/run/.rr-test-build-mode"
    case "$mode" in
        asan) flags=address ;;
        ubsan) flags=undefined ;;
        san) flags=address,undefined ;;
        plain) flags='' ;;
        *) echo "Unknown sanitizer mode: $mode" >&2; return 2 ;;
    esac
    local build_key="v2:$mode:media=$media"
    local previous=''
    [[ -r "$stamp" ]] && previous=$(cat "$stamp")
    if [[ "$previous" != "$build_key" || ! -x "$rr_test_root/bin/rrclient" || ! -x "$rr_test_root/bin/rrserver" ]]; then
        echo "QC build: $mode (rebuilding all objects and libraries in the active tree)"
        rm -f -- "$stamp"
        # Invalidate compiled objects, not Make's configuration/submodule targets.
        # -B would force config/*.config.json's intentional error recipe to run.
        find "$rr_test_root/build" -type f -name '*.o' -delete
        if [[ -d "$rr_test_root/callsign-lookup" ]]; then
            find "$rr_test_root/callsign-lookup" -type f -name '*.o' -delete
        fi
        ( cd "$rr_test_root" && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" SANITIZE="$flags" SANITIZE_MEDIA="$media" world )
        printf '%s\n' "$build_key" > "$stamp"
    fi
    if [[ -n "$flags" ]]; then
        mkdir -p "$rr_test_root/audit-logs"
        if [[ "$flags" == *address* ]]; then
            export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1:log_path=$rr_test_root/audit-logs/asan}"
        fi
        if [[ "$flags" == *undefined* ]]; then
            export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1:log_path=$rr_test_root/audit-logs/ubsan}"
        fi
    fi
}

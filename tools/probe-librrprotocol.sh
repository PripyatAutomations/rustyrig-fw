#!/bin/sh
# Check a compiler-usable, installed librrprotocol without package-manager help.
set -eu
compiler=${RRPROTO_CC:-cc}
pc=${RRPROTO_PKG_CONFIG:-pkg-config}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/check.c" <<'EOF'
#include <librrprotocol/rrprotocol.h>
int check_protocol(void) {
    return ws_room_name_valid("#probe") ? 0 : 1;
}
EOF
cd "$tmp"
# Use target linker semantics; this probe only links, never executes, so it
# can be used with cross-compilers (not just the build machine compiler).
target=$($compiler -dumpmachine 2>/dev/null || echo unknown)
case "$target" in
    *mingw*|*windows*) shared_flags='-shared -Wl,--no-undefined' ;;
    *darwin*|*apple*) shared_flags='-dynamiclib -Wl,-undefined,error' ;;
    *) shared_flags='-shared -Wl,-z,defs' ;;
esac
check() {
    flags=$1
    libs=$2
    $compiler ${RRPROTO_CPPFLAGS:-} ${RRPROTO_CFLAGS:-} $flags \
        -fPIC $shared_flags check.c -o check.so \
        ${RRPROTO_LDFLAGS:-} $libs >/dev/null 2>&1
}
if command -v "$pc" >/dev/null 2>&1 && "$pc" --exists librrprotocol; then
    cflags=$("$pc" --cflags librrprotocol)
    libs=$("$pc" --libs librrprotocol)
    if check "$cflags" "$libs"; then
        echo pkgconfig
        exit 0
    fi
fi
if check "" "-lrrprotocol"; then
    echo compiler
else
    echo none
fi

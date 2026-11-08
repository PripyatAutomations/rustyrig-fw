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
check() {
    flags=$1
    libs=$2
    # Build a shared consumer so host-supplied symbols unresolved in the
    # protocol library are not mistaken for a missing linkable library.
    # -z defs requires its own referenced protocol symbol to resolve on ELF.
    $compiler ${RRPROTO_CPPFLAGS:-} ${RRPROTO_CFLAGS:-} $flags \
        -fPIC -shared -Wl,-z,defs check.c -o check.so \
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

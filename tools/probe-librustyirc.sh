#!/bin/sh
# Check a compiler-usable, installed librustyirc without package-manager help.
set -eu
compiler=${IRC_CC:-cc}
pc=${IRC_PKG_CONFIG:-pkg-config}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/check.c" <<'EOF'
#include <librustyirc/rrprotocol.h>
int check_protocol(void) {
    irc_message_t *m = irc_parse_message("PING");
    irc_message_free(m);
    return 0;
}
EOF
cd "$tmp"
check() {
    flags=$1
    libs=$2
    # Build a shared consumer so host-supplied symbols unresolved in the
    # protocol library are not mistaken for a missing linkable library.
    # -z defs requires its own referenced protocol symbol to resolve on ELF.
    $compiler ${IRC_CPPFLAGS:-} ${IRC_CFLAGS:-} $flags \
        -fPIC -shared -Wl,-z,defs check.c -o check.so \
        ${IRC_LDFLAGS:-} $libs >/dev/null 2>&1
}
if command -v "$pc" >/dev/null 2>&1 && "$pc" --exists librustyirc; then
    cflags=$("$pc" --cflags librustyirc)
    libs=$("$pc" --libs librustyirc)
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

#!/bin/sh
# Check a compiler-usable, installed librustyirc without package-manager help.
set -eu
compiler=${IRC_CC:-cc}
pc=${IRC_PKG_CONFIG:-pkg-config}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/check.c" <<'EOF'
#include <librustyirc/irc.h>
int check_protocol(void) {
    irc_message_t *m = irc_parse_message("PING");
    irc_message_free(m);
    return 0;
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
    $compiler ${IRC_CPPFLAGS:-} ${IRC_CFLAGS:-} $flags \
        -fPIC $shared_flags check.c -o check.so \
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
if check "" "-lrustyirc"; then
    echo compiler
else
    echo none
fi

#!/bin/sh
# Probe installed librustyaxe without looking at this checkout or fetching code.
# Output: pkgconfig, compiler, or none. The compiler must accept the public
# header and resolve a real symbol, not merely find a stray .so filename.
set -eu
compiler=${AXE_CC:-cc}
pc=${AXE_PKG_CONFIG:-pkg-config}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/check.c" <<'EOF'
#include <librustyaxe/core.h>
int main(void) {
    dict *d = dict_new();
    dict_free(d);
    return 0;
}
EOF
cd "$tmp"
check() {
    # Compiler command and build flags are developer-supplied make settings.
    # Word splitting is deliberate for options like CC="ccache gcc".
    set -- $compiler
    "$@" ${AXE_CPPFLAGS:-} ${AXE_CFLAGS:-} $1 \
        check.c -o check ${AXE_LDFLAGS:-} $2 >/dev/null 2>&1
}
if command -v "$pc" >/dev/null 2>&1 && "$pc" --exists librustyaxe; then
    cflags=$("$pc" --cflags librustyaxe)
    libs=$("$pc" --libs librustyaxe)
    if check "$cflags" "$libs"; then
        echo pkgconfig
        exit 0
    fi
fi
if check "" "-lrustyaxe"; then
    echo compiler
else
    echo none
fi

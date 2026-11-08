# Shared by the interactive source-tree test launchers.
rr_test_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$rr_test_root"
# Source-only components live beside the executable. Shared external
# dependencies may be in the native loader's normal search paths instead.
if [ ! -r "$rr_test_root/libfwdspmgr.so.0" ]; then
    echo "Missing local libfwdspmgr.so.0; build the source tree first." >&2
    exit 1
fi
export LD_LIBRARY_PATH="$rr_test_root${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if command -v ldd >/dev/null 2>&1 && [ -x "$rr_test_root/bin/rrclient" ]; then
    if ldd "$rr_test_root/bin/rrclient" | grep -q 'not found'; then
        echo "Shared library unavailable to runtime loader; rebuild or install dependency." >&2
        exit 1
    fi
fi
export PATH="$rr_test_root/bin:$rr_test_root/tools:$PATH"
mkdir -p run db audit-logs recordings dev

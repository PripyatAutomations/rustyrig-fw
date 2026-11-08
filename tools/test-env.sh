# Shared by the interactive source-tree test launchers.
rr_test_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$rr_test_root"
for rr_test_library in librrprotocol libfwdspmgr; do
    if [ ! -r "$rr_test_root/$rr_test_library.so.0" ]; then
        echo "Missing local $rr_test_library.so.0; build the source tree first." >&2
        exit 1
    fi
done
# An installed librustyaxe is resolved by the dynamic loader. Only local
# source builds need the checkout-root SONAME.
if ! pkg-config --exists librustyaxe 2>/dev/null; then
    if [ ! -r "$rr_test_root/librustyaxe.so.0" ]; then
        echo "Missing librustyaxe: install librustyaxe-dev or build the local submodule." >&2
        exit 1
    fi
fi
export LD_LIBRARY_PATH="$rr_test_root${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$rr_test_root/bin:$rr_test_root/tools:$PATH"
mkdir -p run db audit-logs recordings dev

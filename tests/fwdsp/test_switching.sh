#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Real codec elements, with no sound device required for decoder lifecycle tests.
sed 's/pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false/appsink name=rx-sink sync=false/g' config/rrserver.cfg > "$work/test.cfg"
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   ${SWITCHING_CFLAGS:-} tests/fwdsp/switching.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -lrrprotocol -o "$work/switching"
export LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 - "$work/switching" "$work/test.cfg" <<'PYTHON'
import os, signal, subprocess, sys
proc = subprocess.Popen(sys.argv[1:], start_new_session=True)
try:
    result = proc.wait(timeout=90)
finally:
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
sys.exit(result)
PYTHON

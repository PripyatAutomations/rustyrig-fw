#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Real codec elements, with no sound device required for decoder lifecycle tests.
server_cfg=${RRSERVER_CONFIG:-config/rrserver.cfg}
[[ -f "$server_cfg" ]] || server_cfg=config/rrserver.cfg
python3 - "$server_cfg" "$work/test.cfg" <<'PYTHON'
import re, sys
from pathlib import Path
text = Path(sys.argv[1]).read_text()
text = re.sub(r'\\\n\s*', ' ', text)
text = text.replace('pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false',
                    'appsink name=rx-sink sync=false')
text = re.sub(r'(?m)^([A-Za-z0-9]{4}\.tx=)appsrc[^!]*!',
              r'\1audiotestsrc is-live=true wave=pink-noise volume=0.15 !', text)
Path(sys.argv[2]).write_text(text)
PYTHON
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} -ffunction-sections -fdata-sections \
   ${SWITCHING_CFLAGS:-} fwdsp/tests/switching.c -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
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

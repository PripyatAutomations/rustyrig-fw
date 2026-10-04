"""Startup failures must never flush diagnostics into the binary media pipe."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path.cwd()
with tempfile.TemporaryDirectory() as temp:
    config = Path(temp) / "fwdsp.cfg"
    config.write_text("[fwdsp]\nlog.file=-\n")
    result = subprocess.run(
        [str(root / "bin/fwdsp"), "-f", str(config), "-c", "xxxx"],
        input=b"", capture_output=True, timeout=5,
        env=dict(os.environ, LD_LIBRARY_PATH=str(root)),
    )
    assert result.returncode != 0, "missing codec pipeline unexpectedly started"
    assert b"No pipeline configured" in result.stderr, result.stderr
    assert result.stdout == b"", repr(result.stdout)
print("PASS: startup errors keep log banners out of framed stdout")

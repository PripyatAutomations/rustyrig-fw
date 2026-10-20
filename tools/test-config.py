#!/usr/bin/env python3
"""Generate source-tree-only configs from the shipped package examples."""
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
program = sys.argv[2]
assert program in ("rrclient", "rrserver")
for directory in ("run", "db", "audit-logs", "recordings", "dev"):
    (root / directory).mkdir(exist_ok=True)
replacements = {
    "/var/lib/rustyrig/db": str(root / "db"),
    "/var/lib/rustyrig/sql": str(root / "sql"),
    "/var/lib/rustyrig/www": str(root / "www"),
    "/var/lib/rustyrig/help": str(root / "help"),
    "/var/lib/rustyrig/tools": str(root / "tools"),
    "/var/lib/rustyrig/recordings": str(root / "recordings"),
    "/var/log/rustyrig": str(root / "audit-logs"),
    "/var/log/fwdsp-cli.log": str(root / "audit-logs/fwdsp-cli.log"),
    "/var/run/rustyrig": str(root / "run"),
    "/usr/lib/rustyrig/modules/rrclient": str(root / "bin"),
    "/usr/lib/rustyrig/modules/rrserver": str(root / "bin"),
    "/usr/bin/callsign-lookup": str(root / "bin/callsign-lookup"),
    "~/.config/rustyrig/callsigns.db": str(root / "db/rrclient-callsigns.db"),
    "/etc/rustyrig/": str(root / "config") + "/",
}
os.umask(0o077)
# Mirror current config fragments so relative !include directives resolve
# from run/ without requiring retired callsign-lookup.*.cfg files.
source = root / "config" / (program + ".cfg")
if not source.is_file():
    sys.exit(f"Missing {source}")
for path in sorted((root / "config").glob("*.cfg")):
    content = path.read_text()
    for original, local in replacements.items():
        content = content.replace(original, local)
    if path == source:
        content += f"\n[general]\nfwdsp:path={root}/bin/fwdsp\n"
        if program == "rrclient":
            content += f"log.file={root}/audit-logs/rrclient.log\ncat.pty.path={root}/dev/ttyCAT0\n"
    (root / "run" / path.name).write_text(content)
print(root / "run" / (program + ".cfg"))

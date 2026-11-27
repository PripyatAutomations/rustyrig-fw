#!/usr/bin/env python3
"""Apply public station metadata without rewriting the rest of rrserver's config."""
# SPDX-License-Identifier: GPL-2.0-only
import os
import pathlib
import re
import stat
import sys
import tempfile


def configure(path, values):
    path = pathlib.Path(path).resolve()
    updates = {}
    for key, value in zip(("station.call", "station.grid", "station.location", "station.description", "domain", "station.name"), values):
        if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
            raise ValueError(f"Invalid control character in {key}")
        value = value.strip()
        if value:
            if key in ("domain", "station.name"):
                labels = value.split(".") if key == "domain" else [value]
                if len(value) > 253 or any(not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?", label) for label in labels):
                    raise ValueError(f"Invalid DNS label in {key}")
            updates[key] = value
    if not updates:
        return

    original = path.read_bytes() if path.exists() else b""
    lines = original.decode("utf-8", "surrogateescape").splitlines(keepends=True)
    section = re.compile(r"^\s*\[([^]]+)\]\s*(?:[;#].*)?$")
    in_general = False
    general_end = None
    seen = set()
    for i, line in enumerate(lines):
        body = line.rstrip("\r\n")
        match = section.fullmatch(body)
        if match:
            if in_general:
                general_end = i
            in_general = match[1].strip() == "general"
        if not in_general:
            continue
        general_end = i + 1
        left, sep, right = body.partition("=")
        key = left.strip()
        if sep and key in updates:
            seen.add(key)
            if right.strip() != updates[key]:
                lines[i] = left + "=" + updates[key] + (line[len(body):] or "\n")

    missing = [f"{key}={value}\n" for key, value in updates.items() if key not in seen]
    if missing:
        if general_end is None:
            general_end = len(lines)
            missing.insert(0, "[general]\n")
        if general_end and not lines[general_end - 1].endswith(("\n", "\r")):
            lines[general_end - 1] += "\n"
        lines[general_end:general_end] = missing

    result = "".join(lines).encode("utf-8", "surrogateescape")
    if result == original:
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    existing = path.stat() if path.exists() else None
    fd, tmp = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(result)
            if existing:
                os.fchown(out.fileno(), existing.st_uid, existing.st_gid)
                os.fchmod(out.fileno(), stat.S_IMODE(existing.st_mode))
            else:
                os.fchmod(out.fileno(), 0o644)
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


if __name__ == "__main__":
    if len(sys.argv) not in (6, 8):
        sys.exit("Usage: configure-site CONFIG CALL GRID LOCATION DESCRIPTION [DOMAIN NODE]")
    try:
        configure(sys.argv[1], sys.argv[2:])
    except (OSError, ValueError) as error:
        sys.exit(f"rustyrig-server: cannot configure station metadata: {error}")

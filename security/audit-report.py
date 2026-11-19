#!/usr/bin/env python3
"""Compare static analyzer findings conservatively across RustyRig audit runs."""
import argparse
import hashlib
import json
import plistlib
import re
from pathlib import Path

SCANNERS = ("scanbuild", "clang-tidy", "cppcheck", "flawfinder", "printf")
DIAGNOSTIC = re.compile(r"^(?P<file>[^:\n]+\.(?:c|h|cc|cpp)):(?P<line>\d+)(?::\d+)?: (?P<severity>warning|error|note): (?P<message>.+?)(?: \[(?P<code>[-\w.]+)\])?$", re.I)
FLAW = re.compile(r"^(?P<file>.+?):(?P<line>\d+):\s*\[(?P<score>[0-5])\]\s*\((?P<category>[^)]*)\)\s*(?P<message>.+)$")
CPPCHECK = re.compile(r"^\[(?P<file>.+?):(?P<line>\d+)\]:\s*\((?P<severity>error|warning|style|performance|portability|information)\)\s*(?P<message>.+)$", re.I)
PSCAN = re.compile(r"^(?P<file>.+?):(?P<line>\d+) Warning: (?P<message>.+)$")
OWNED = ("rrclient", "rrserver", "fwdsp", "libfwdspmgr", "librrsdrutils",
         "rrbridge", "rrsdrbridge", "rrpipelines")


def source_path(path):
    path = path.replace("\\", "/")
    for part in OWNED:
        marker = part + "/"
        if path.startswith(marker) or "/" + marker in path:
            return marker + path.split(marker, 1)[1]
    return path


def finding(scanner, file, line, severity, code, message):
    file = source_path(file)
    message = " ".join(message.split())
    # Stable when a line moves; scanner, file, rule and message identify an issue.
    fingerprint = hashlib.sha256(json.dumps([scanner, file, code, message]).encode()).hexdigest()[:20]
    return {"id": fingerprint, "scanner": scanner, "file": file, "line": int(line),
            "severity": severity, "rule": code, "message": message}


def parse(scanner, log):
    result = {}
    for text in log.read_text(errors="replace").splitlines():
        issue = None
        if scanner == "printf":
            m = PSCAN.match(text)
            if m:
                issue = finding(scanner, m["file"], m["line"], "moderate",
                                "pscan.variable-format", m["message"])
        elif scanner == "flawfinder":
            m = FLAW.match(text)
            if m:
                score = int(m["score"])
                issue = finding(scanner, m["file"], m["line"],
                                "severe" if score >= 4 else "moderate" if score >= 2 else "low",
                                "flawfinder." + str(score), m["category"] + ": " + m["message"])
        else:
            m = DIAGNOSTIC.match(text) or (CPPCHECK.match(text) if scanner == "cppcheck" else None)
            if m:
                code = m.groupdict().get("code") or scanner + ".generic"
                severe = m["severity"].lower() == "error" or (
                    scanner == "clang-tidy" and code.startswith("clang-analyzer-"))
                severity = "severe" if severe else "moderate" if m["severity"].lower() == "warning" else "low"
                issue = finding(scanner, m["file"], m["line"], severity, code, m["message"])
        if issue:
            result[issue["id"]] = issue
    return result


def parse_scanbuild(directory):
    """Read Clang's native plist diagnostics rather than scraping HTML."""
    result = {}
    for path in (directory / "scanview").rglob("*.plist") if (directory / "scanview").exists() else []:
        try:
            with path.open("rb") as stream:
                report = plistlib.load(stream)
        except (OSError, ValueError, plistlib.InvalidFileException):
            continue
        files = report.get("files", [])
        for item in report.get("diagnostics", []):
            loc = item.get("location", {})
            index = loc.get("file", -1)
            if not isinstance(index, int) or index < 0 or index >= len(files):
                continue
            category = item.get("category", "Clang analyzer")
            rule = item.get("type", "unknown")
            message = item.get("description", rule)
            severity = "low" if category == "Unused code" else "severe" if category in (
                "Memory error", "Logic error", "API", "Stream handling error") else "moderate"
            issue = finding("scanbuild", files[index], loc.get("line", 0), severity, rule, message)
            result[issue["id"]] = issue
    return result


def load(path):
    return json.loads(path.read_text()) if path.is_file() else {"findings": [], "scanners": {}}


def collect(directory, prior):
    before = {f["id"]: f for f in prior.get("findings", [])}
    items, statuses = {}, {}
    for scanner in SCANNERS:
        status_path = directory / (scanner + ".status")
        log = directory / (scanner + ".log")
        status = status_path.read_text().strip() if status_path.is_file() else "unknown"
        if status not in ("ok", "failed", "skipped") or (status == "ok" and not log.is_file()):
            status = "unknown"
        statuses[scanner] = status
        if status == "ok":
            items.update(parse_scanbuild(directory) if scanner == "scanbuild" else parse(scanner, log))
        else:
            # Missing/failed scan cannot resolve previously known vulnerabilities.
            items.update({k: v for k, v in before.items() if v["scanner"] == scanner})
    return {"version": 1, "scanners": statuses,
            "findings": sorted(items.values(), key=lambda f: (f["scanner"], f["file"], f["line"], f["id"]))}


def compare(old, new):
    before = {f["id"]: f for f in old.get("findings", [])}
    after = {f["id"]: f for f in new["findings"]}
    checked = {name for name, status in new["scanners"].items() if status == "ok"}
    return {
        "new": [f for k, f in after.items() if k not in before and f["scanner"] in checked],
        "resolved": [f for k, f in before.items() if k not in after and f["scanner"] in checked],
        "open": [f for k, f in after.items() if k in before and f["scanner"] in checked],
        "not_checked": [f for f in after.values() if f["scanner"] not in checked],
    }


def report(snapshot, changes, label):
    lines = ["# RustyRig security audit comparison", "", "Compared with: " + label, "",
             "Scanner status: " + ", ".join(k + "=" + v for k, v in snapshot["scanners"].items()), ""]
    sections = (("New severe", "new", "severe"), ("Resolved severe", "resolved", "severe"),
                ("New other", "new", "other"), ("Resolved other", "resolved", "other"),
                ("Still open", "open", None), ("Not checked", "not_checked", None))
    for title, category, severity in sections:
        rows = [x for x in changes[category] if severity is None or
                (x["severity"] == "severe") == (severity == "severe")]
        lines.extend(["## " + title + " (" + str(len(rows)) + ")", ""])
        for f in sorted(rows, key=lambda x: (x["scanner"], x["file"], x["line"])):
            lines.append("- **" + f["severity"] + "** " + f["scanner"] + " \x60" +
                         f["file"] + ":" + str(f["line"]) + "\x60 [" + f["rule"] + "]: " + f["message"])
        if not rows:
            lines.append("None")
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    parser.add_argument("--state", type=Path, default=Path("security/audit-logs/state.json"))
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--fail-on-new-severe", action="store_true")
    opts = parser.parse_args()
    old = load(opts.state)
    current = collect(opts.run, old)
    if opts.baseline and not opts.baseline.is_file():
        parser.error("baseline does not exist: " + str(opts.baseline))
    reference = load(opts.baseline) if opts.baseline else old
    delta = compare(reference, current)
    opts.run.mkdir(parents=True, exist_ok=True)
    (opts.run / "findings.json").write_text(json.dumps(current, indent=2) + "\n")
    (opts.run / "comparison.json").write_text(json.dumps(delta, indent=2) + "\n")
    message = report(current, delta, str(opts.baseline) if opts.baseline else "previous audit run")
    (opts.run / "comparison.md").write_text(message)
    opts.state.parent.mkdir(parents=True, exist_ok=True)
    opts.state.write_text(json.dumps(current, indent=2) + "\n")
    for line in message.splitlines():
        if line.startswith("## ") or line.startswith("Scanner status:"):
            print(line)
    print("Full report: " + str(opts.run / "comparison.md"))
    if opts.fail_on_new_severe and any(f["severity"] == "severe" for f in delta["new"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()

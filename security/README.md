# Security audit and historical comparison

Run `make audit` to execute the existing Clang Static Analyzer,
Clang-Tidy, Cppcheck, Flawfinder, and pscan checks. Audit output is written
to `security/audit-logs/<timestamp>/`; `latest` points to the newest run.

The final step produces:

- `findings.json`: current normalized findings and scan completeness
- `comparison.json`: new, resolved, still-open, and not-checked entries
- `comparison.md`: readable summary, **new severe** and **resolved severe**
  first

The state file `security/audit-logs/state.json` retains the previous findings
between runs. Generated logs and state are intentionally not committed.

## Pinned baseline

After a representative full audit, run:

```sh
make audit-baseline
AUDIT_BASELINE=security/baseline.json make audit
AUDIT_BASELINE=security/baseline.json AUDIT_FAIL_NEW_SEVERE=1 make audit
```

The first command snapshots the latest result into `security/baseline.json`.
Use a pinned baseline for longer-term comparisons; omit `AUDIT_BASELINE`
to compare against the previous run. The optional severity gate returns failure\nwhen new severe findings appear, without discarding reports. Keep the baseline in version control if
you want the same reference on other machines.

## Severity and completeness

The initial severity mapping is deliberately simple:

- Scan-build memory, logic, API, and stream findings: severe; unused code: low; others: moderate

- Clang-Tidy's `clang-analyzer-*` diagnostics: severe; other warnings: moderate
- Cppcheck errors: severe; warnings: moderate
- Flawfinder score 4–5: severe; 2–3: moderate; 0–1: low

These are triage labels, **not CVSS scores** or proof of exploitability.
Finding IDs include scanner, source path, rule and message, but not line number;
moving code does not usually create a false new/resolved pair.

A skipped, failed, or unknown scanner retains its last findings as
**not checked**, not resolved. Check `*.status` and raw logs when comparing.
Scan-build, Cppcheck, Clang-Tidy and Flawfinder are normalized. Scan-build
preserves Clang plist findings under the run directory's `scanview/` directory.
pscan continues to save its native report, but is not yet normalized. An incomplete profile or modified analyzer
configuration may also change which code is inspected. Compare like builds.

Run `tests/selftest/test_audit_report.sh` to validate the comparison logic.
The regular `make test` suite discovers this test automatically.

## Network trust boundaries

Prioritize coverage of the actual wire parsers from their owning repository
(`librrprotocol` is a separate installed dependency), followed by client-side
untrusted-server messages, server command authorization, and signed mesh
catalog ownership. State-machine tests should use disposable credentials,
databases, and loopback listeners; never production mesh nodes or RF hardware.

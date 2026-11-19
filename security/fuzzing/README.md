# Security fuzzing and static review

These are **regression probes**, not proof of security. Run them before a
club preview or release candidate and investigate all new crashes, hangs,
authorization failures and analyzer findings. No fuzzing should involve real
radio transmitters, untrusted external networks or live mesh peers.

## Native WebSocket boofuzz

Set up an **isolated, disposable** rrserver with no hardware/PTT outputs,
no live peers, and a loopback-only listener. The script rejects non-loopback
hostnames/IPs. It uses TLS (the normal RustyRig native transport); server
certificate verification is **enabled by default**.

    python3 -m venv .venv-fuzz
    .venv-fuzz/bin/pip install 'boofuzz>=0.4.2,<0.5'
    .venv-fuzz/bin/python security/fuzzing/boofuzz_ws.py --mode upgrade \
        --port 4420 --ca-file /path/to/test-ca.pem --server-name localhost
    .venv-fuzz/bin/python security/fuzzing/boofuzz_ws.py --mode frames \
        --port 4420 --ca-file /path/to/test-ca.pem --server-name localhost

For an ephemeral self-signed local test certificate, explicitly add
\`--insecure-loopback-tls\` rather than disabling verification by default.
The scripts use boofuzz's TLS socket transport, run at most 150 cases by
default (\`--cases N\` to adjust), and write results to
\`security/fuzzing/results/\`. Use separate, disposable servers and unique
result filenames if keeping more than one run; a run can interrupt service.

- **upgrade** mutates path, Upgrade/Connection/version/key/subprotocol
  headers, and length constraints while staying unauthenticated.
- **frames** performs a valid native WebSocket upgrade on every connection
  and then mutates a masked client text frame. Invalid opcode/mask/length
  combinations may produce expected disconnects.
- Inspect **server** logs, core dumps, ASan/UBSan output, and boofuzz's
  result DB. Connection resets are not automatically vulnerabilities, and
  a running server is not evidence of correct authorization.
- Not covered: signed mesh catalog state machines, credential/session
  authorization, PTT safety, replay protection, or authenticated WebSocket
  binary operations. Add isolated model/integration harnesses for those
  before claiming hostile-peer resilience.

Use this under a non-privileged account and **never** run it against a
real station on its production listener.

## Semgrep review rules

    python3 -m pip install semgrep
    semgrep scan --config security/fuzzing/semgrep.yml \
        rrserver rrclient libfwdspmgr rrbridge rrsdrbridge
    semgrep scan --config security/fuzzing/semgrep.yml --json \
        --output security/fuzzing/results/semgrep.json .

Rules flag disabled TLS validation, shell execution, unbounded formatting,
unbounded copies in mesh/relay sources, and an unsafe remote-admin
configuration default. Warnings are *review items*; checked and bounded
calls may be false positives. These rules deliberately do **not** auto-fix
source, alter runtime privileges or suppress scanner findings.

The first scan is a baseline, not a regression. Review it, record justified
exceptions in source or issue trackers, and compare future JSON reports.
Semgrep, boofuzz and their databases are optional development tools, not
production runtime dependencies.

## Preview security blockers

In addition to these probes, verify:
- A new database has **no** blanket cross-site grants or remote-admin role.
- Existing databases have been reviewed and **explicitly** had old test grants
  revoked; replacing a seed SQL file does not update existing databases.
- `remote-admin.next-hop-only=true` is active and multi-hop attempts fail.
- Unauthenticated WebSocket requests cannot tune, key TX, modify files or
  publish catalog entries.
- The server dekeys RF/PTT on client disconnect, peer loss and subprocess
  failure; test with dummy output or a safe nontransmitting load.

Keep a record of the tested commit and security/fuzzing tool versions with
each preview candidate.

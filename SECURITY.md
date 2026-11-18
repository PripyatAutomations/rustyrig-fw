# Security Policy

## Supported Versions
Please make sure you are running the latest version before reporting a bug or requesting a feature!

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

Please submit a PR or contact me on IRC.

## Auditing infrastructure

Static analysis and some basic fuzzing is built in security/

You can run make audit to cause security/audit-logs/ to populate with reports.

Much of this will be noise, but it may help find regressions.

Clanker generated tests exist, we need to audit and improve them!


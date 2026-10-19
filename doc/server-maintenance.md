# rrserver maintenance commands

These one-shot commands are intended for administrators working on a **stopped** server. Each requires an interactive terminal, prints the exact target path and operation, and asks you to type **`YES <full-option>`**. There are no short forms, no abbreviated long options, and **no `--force` bypass**. A cancelled operation changes nothing.

Use the same configuration file as the server you intend to maintain:

```sh
rrserver -f /etc/rustyrig/rrserver.cfg --reset-rooms
```

The command performs the requested action and **exits without starting rrserver**. The configured database is protected by an advisory `.rrlock` file while the v1 server runs and during maintenance. Stop all older server processes before running these tools; older development builds do not participate in this locking scheme. Back up your database and recordings before destructive maintenance.

| Option | Effect |
| --- | --- |
| `--purge-recordings` | Removes known audio file types (FLAC, WAV, Ogg, Opus, MP3, AAC, M4A, PCM, RAW) beneath the configured `fwdsp:recording.path`. Does not follow symlinks or delete unrelated files. |
| `--reset-db` | Deletes the entire master SQLite database and its associated WAL/SHM and bootstrap-password files. **All** local users, credentials, rooms, histories, audits, quotas, and stored identities are lost. A new database is provisioned on the next regular startup. |
| `--reset-rooms` | Removes room metadata and VFO-room bindings. Chat history remains in the database, but its old room names may no longer be visible until recreated. |
| `--reset-logs` | Clears chat and PTT logs; does not clear the audit log or remove audio recording files. |
| `--reset-audit` | Clears the audit log only. |
| `--reset-adminpw` | Prompts twice for the new administrator password with terminal echo disabled, then updates the account directly. Requires an interactive terminal; no random bootstrap credential is generated. |
| `--reset-uuids` | Generates new room UUIDs and clears stored server, configured-rig, and VFO UUID mappings; does not remove rooms, users, chat history, or room topics. Room-to-VFO bindings are cleared because they reference the old VFO UUIDs. |

The `--reset-db`, `--reset-rooms`, `--reset-logs`, `--reset-audit`, `--reset-adminpw` and `--reset-uuids` actions operate on `path.db.master` from the effective rrserver configuration. Do not guess which database is active when multiple instances are installed.

## Identity rules for v1

The clean v1 database assigns a durable UUID to every persistent room; this UUID is sent in resource inventory and is independent of the readable room name. The local server/node UUID is bound to the database, not `station.name`. Configured rig and VFO UUIDs are persisted using stable configuration identities. Persistent audio, GPS/NMEA and station webcam stream UUIDs are deterministically derived from their owning rig or server UUID, so reconnecting subscribers see the same logical resource after restart.

Transient connections and temporary SDR bridge instances use fresh UUIDs and are **not** expected to retain identity across reconnects. An SDR room that persists as a room is distinct from a temporary bridge's session identity.

`--reset-uuids` is intentionally disruptive to cached topology and object subscriptions: all clients and linked servers need to reconnect/re-discover afterwards. Use it only when you want to intentionally create new identities for the same named resources.

**Compatibility policy:** v1.0 is the initial protocol and database-schema baseline. Pre-v1 development builds are not a supported wire-compatibility target. A fresh v1 database is recommended when adopting the new room UUID schema; do not expect old development databases to migrate automatically.

## Troubleshooting

A maintenance command fails without changes when it cannot open its configured target, another v1 server holds the database lock, no terminal is available for confirmation, or the exact confirmation is not entered. For admin password reset, check whether a previous bootstrap password file already exists. If a reset fails after writing has started, inspect the reported error and restore from backup rather than repeating it blindly.

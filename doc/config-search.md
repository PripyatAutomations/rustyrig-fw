# GTK Config field search

The search box in the Config tab filters fields as you
type. It is case-insensitive and treats the search text literally:

- `audio`: show fields whose key or description contains `audio`.
- `=audio`: show fields whose key contains `audio`, ignoring descriptions.
- `^debug`: show fields whose key starts with `debug`.
- Empty search (or just `=` or `^`): show all fields.

This is not a regular expression search. A search may match no fields; for
example, current logging keys use `log.` rather than the old `debug.` prefix.
Filtering hides whole rows and keeps their edited values. Clear the search to
see every field again, and click **Save Config** to save the effective configuration to `client.db`.

System `rrclient.cfg` and optional user configuration provide bootstrap defaults. At
startup the saved SQLite snapshot takes precedence. Launch with
`rrclient --ignore-db-config` to skip only the saved configuration overrides
at startup; saved server profiles and themes still load from their separate DB
tables. If the override snapshot is unreadable, startup falls back to file
defaults rather than exiting. The **Reload Config** button offers a file-only
reload or a file-plus-DB reload; neither deletes saved settings. The **Edit Raw** button is no longer
in the Config tab; the same editor remains available through Remote Files.

The GTK Config tab labels the search box **Filter:** and shows an empty-field
hint (`Filter config… (=key, ^prefix)`). The tooltip explains the matching modes.

## Reset the client database

Run `rrclient --reset-db` from an interactive terminal while all other
`rrclient` instances are stopped. The command prints the resolved client
database path and requires the exact response `YES --reset-db` before
removing it. It deletes `client.db` and associated SQLite WAL, shared-memory
and rollback-journal files, then exits. The next normal startup creates
a new database.

The default location is `~/.local/state/rustyrig/client.db`, or
`$XDG_STATE_HOME/rustyrig/client.db` when XDG_STATE_HOME is set. This reset
removes saved server profiles, networks, themes, window layouts and saved
configuration overrides. It does **not** remove certificate identities,
`rrclient.cfg`, or other files outside the client database. Back up the
database first if you need to preserve those settings.

If you only need to disregard saved configuration overrides temporarily,
use `rrclient --ignore-db-config` instead.

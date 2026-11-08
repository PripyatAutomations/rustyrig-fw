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

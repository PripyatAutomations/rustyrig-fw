# GTK Config field search

The search box to the left of the SAVE CONFIG reminder filters fields as you
type. It is case-insensitive and treats the search text literally:

- `audio`: show fields whose key or description contains `audio`.
- `=audio`: show fields whose key contains `audio`, ignoring descriptions.
- `^debug`: show fields whose key starts with `debug`.
- Empty search (or just `=` or `^`): show all fields.

This is not a regular expression search. A search may match no fields; for
example, current logging keys use `log.` rather than the old `debug.` prefix.
Filtering hides whole rows and keeps their edited values. Clear the search to
see every field again, and click **Save Config** to save changes as usual.

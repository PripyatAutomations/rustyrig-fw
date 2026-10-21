# FT-891 on-demand EX menu read testing

Read-only menu value support is implemented using a generic backend
`menu_read` callback, serialized on the backend worker. No menu values
are polled unless requested.

Open **Resource Explorer → FT-891 → MENU → 18 VERSION → 18-03 LCD VERSION**
by double-clicking. The GTK popup issues:
`property.cmd=read`, `target=<rig uuid>`, `property.name=menu.1803`.
The worker sends `EX1803;`; a valid response `EX1803<parameter>;`
is stored as string property `menu.1803` and published on the object stream.
The popup shows the raw parameter as **read-only**.

Try some other known simple entries, e.g. `0201` contrast, and
`0504` beep level. The user must compare the raw numeric value to
the rig's screen; human-friendly decoded enumerations are future work.

The server rejects unknown menu IDs; the FT-991A menu reader remains
disabled until its EX command semantics have been verified. No EX writes
are implemented.

Known limitations: read failure surfaces as unavailable in the shared
property store; the popup has a ten-second timeout but does not yet
correlate individual replies to its request ID. Until response/version
tracking is added, a previously cached value could be shown after a
failed refresh. This needs hardening before claiming guaranteed fresh
readback. Both server and GTK client need building and hardware tests.

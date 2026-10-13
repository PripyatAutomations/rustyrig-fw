# Reconnect intervals and administration

Server links use `[general] links.retry-seconds=60`. Each enabled `[peer]` or
`[peer:name]` can set `retry-seconds=1..3600`; an absent or empty value inherits
the global interval. Initial autoconnect happens immediately. Failed or lost
links wait the configured interval before retrying.

Authenticated admins and owners can use `/link list`, `/link open <name>`,
and `/link close <name>`. Names are stored peer section names, or the portion
after `peer:`. The unnamed `[peer]` is addressed as `peer`. OPEN requires a
stored outgoing URL and enables retries for the running process. CLOSE drops
the link and blocks both outgoing retries and incoming redials until OPEN or
server restart. Commands do not rewrite configuration or bypass authentication,
ACLs, or tree admission.

Native client connections use `[general] server.reconnect-interval=60`, with
`server.reconnect-interval` in `[server:name]` as an optional override. Values
are seconds, 1..3600; invalid values fall back to 60. The existing ten-attempt
limit and explicit disconnect behavior remain. Browser configuration has a
global reconnect interval and an optional current-server override, stored
locally per WebSocket URL.

IRC numeric 433 emits `irc.nick-in-use`; it has no effect on RustyRig login
names. GTK prompts for a replacement nickname for 45 seconds. Closing the
prompt leaves the timer running. TUI prints an error suggesting `/nick` and
starts fallback immediately. Fallback tries each of `_{}[]-^`, then every
two-character combination, truncating the original nickname to fit NICKLEN.
A rejected replacement chosen by the user uses that replacement as its base.
After all 56 alternatives are rejected, the connection closes without retrying.
An accepted nickname, successful registration, or disconnect clears recovery.

GTK and browser PTT controls latch an orange `TOT EXPIRE` warning after server
TOT shutdown. State updates do not clear it. The next click acknowledges
locally without sending a PTT command; subsequent clicks work normally.
GTK PTT shortcut acknowledgement also consumes repeats and release.

The Admin page requests SQLite users, rooms, audit events, and PTT sessions.
Inventory includes offline and locked users and deleted rooms. Edit changes
account privileges or room topics; Lock/Unlock and Remove/Restore use existing
server commands and their permissions. Accounts never expose password hashes.
Each response page contains at most 100 rows, newest first. Load older / more
uses an exclusive row ID cursor, avoiding duplicate rows when logs grow.
Refresh starts a new listing. GTK binds each table and action to its issuing
server profile; browser actions use its current connection.

`/admin users|rooms|audit|ptt [before-id]` provides the same inventory in TUI.
The server authorizes each request independently. Additive `talk.link` and
`talk.admin` operations are registered in the authoritative wire schema.
Admin responses use `talk.target` for the table, `talk.action` for
`begin|row|end`, and JSON text in `talk.data`. BEGIN contains `append`; ROW
contains selected database fields; END contains `next` (0 means no more rows).
SQLite remains owned by rrserver. Libraries dispatch application events and
frontends present the results.

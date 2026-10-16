# Source organization review — 15 October 2026

## Counting and scope

Aim for 500–800 code lines per file, with 1500 as the hard cap. Count physical
lines containing code; exclude blank lines, comments and license boilerplate.
Braces, declarations, data tables and preprocessor directives count. Inspect
owned code only; do not edit `ext/`, generated output or bundled third-party JS.

`python3 tools/source-size.py` inventories C and headers in the main components,
including callsign lookup and target/binding headers. `--json` includes function
sizes and source locations; `--max-lines 1500` checks the hard file cap. Function
boundaries are lexical estimates, especially around preprocessor alternatives.
The shared selftest suite checks counting edge cases and the owned C file cap.
JavaScript counts below used Acorn's parsed comment ranges (UTF-16 offsets),
which correctly distinguish comments from regular-expression literals. An
initial C-style scan overstated the old browser file size; the parsed value is
1445, not 1539. All owned `www/js/webui*.js` files were checked this way.

This pass splits the three C files over the hard cap, plus the browser chat file
above the preferred range. The remaining C files above 800 are inventoried
below for subsequent focused domain reviews; they remain within the hard cap.
This is an organization pass, not a claim of a complete security audit or CPU
optimization of every inventoried file.

## Completed splits

| Original | Before code lines | Resulting files and code lines |
| --- | ---: | --- |
| `rrserver/relay.c` | 2129 | `relay.c` 726; `relay.catalog.c` 521; `relay.media.c` 369; `relay.sessions.c` 555 |
| `rrserver/events.c` | 1767 | `events.c` 307; `events.rooms.c` 495; `events.rig.c` 200; `events.quota.c` 246; `events.users.c` 459; `events.admin.c` 245 |
| `rrserver/database.c` | 1563 | `database.c` 633; `database.rooms.c` 341; `database.users.c` 230; `database.accounting.c` 416 |
| `www/js/webui.chat.js` | 1445 | `webui.chat.js` 570; `webui.chat.help.js` 311; `webui.chat.users.js` 223; `webui.chat.commands.js` 344 |

New C units retain project license headers and required declarations. Function
comments describe responsibilities and ownership-sensitive behavior. Private
headers carry cross-domain server declarations, not public protocol changes.
Each shared relay state value has one definition in its owning domain; values
used by only that domain remain static. Event domains register their static
listeners through `event_on()`/`event_on_binary()` using the existing callbacks.

The browser retains existing globals and the ordered asset loader. Its media
command body is now a separate helper, bringing `parse_chat_cmd()` below 300
code lines. All functions in the split C files are below 300 code lines. No
client commands or wire semantics change. A previously unguarded relay session
visitor is explicitly a no-op in builds without Mongoose/server links.

## VAL_PTR ownership

Production uses are data objects, not just callbacks:

- `librustyaxe/event-bus.c` stores `event_entry_t *` records containing listener
  arrays and generation counters. Event-bus cleanup frees these objects.
- `rrserver/rig.properties.c` stores `rr_rig_property_t *` objects containing
  schema, owned strings and typed values. Property-store cleanup frees them.

`dict_clone()` independently owns keys/string values and preserves scalar types;
`VAL_PTR` remains a borrowed alias. A clone must not assume the pointed object
was cloned or that it stays alive after its owning subsystem destroys it.
JSON encoding converts `VAL_PTR` to null; replacing a JSON round trip is safe
only when the caller understands that distinction. Relay wire dictionaries and
native object-cache messages contain decoded wire values, not event/property
store pointers. A generic deep pointer clone would need an explicit ownership
or per-type callback contract.

## Remaining files above the preferred range

These are proposed boundaries, not changes already made. Keep authoritative
behavior in its current component and avoid making private details public just
for a mechanical split.

| File | Code lines | Candidate responsibilities |
| --- | ---: | --- |
| `librustyaxe/termkey.c` | 1481 | Key naming/formatting, key parsing, and terminal lifecycle/input. |
| `librrprotocol/srv.chat.c` | 1463 | Room operations, account/moderation commands, and dispatch. |
| `librustyaxe/dict.c` | 1447 | Typed accessors, iteration/merge, and storage/rehash internals. |
| `fwdsp/fwdsp.c` | 1285 | Pipeline construction, control messages, and run-loop sample processing. |
| `rrclient/gtk/gtk.userlist.c` | 1268 | Roster model updates, layout sizing, and interaction/menu code. |
| `libfwdspmgr/fwdsp-mgr.c` | 1260 | Process lifecycle, configured pipeline construction, and frame delivery. |
| `rrclient/events.c` | 1226 | Common connection, chat/roster, and rig-state event domains. |
| `rrclient/gtk/gtk.chat.c` | 1216 | Text rendering/link handling, input interaction, and widget lifecycle. |
| `librustyaxe/config.c` | 1212 | Config parsing, persistence, and default/schema accessors. |
| `rrclient/media.c` | 1124 | Catalog/lookup, subscriptions/transport, and command presentation. |
| `rrclient/connman.c` | 999 | Transport setup, connection lifecycle, and reconnect policy. |
| `rrserver/bridge.manager.c` | 997 | Child lifecycle, rig control/metadata, and dynamic receiver admission. |
| `rrclient/receiver.directory.c` | 950 | Directory parsing/cache, fetch lifecycle, and receiver commands. |
| `callsign-lookup/src/callsign-lookup.c` | 939 | Provider/config handling and query/response lifecycle. |
| `rrclient/gtk/gtk.core.c` | 939 | Formatting/rendering, widget setup, and frontend lifecycle. |
| `rrserver/rig.properties.c` | 900 | Property storage/schema, value access, and backend synchronization. |
| `rrserver/disabled/dds.ad9959_stm32.c` | 890 | Disabled hardware driver: separate programming sequences if re-enabled. |
| `librustyaxe/json.c` | 834 | Typed JSON encoding and decoding helpers. |
| `rrserver/link.c` | 818 | Link transport/handshake, peer management, and application routing. |
| `librrprotocol/ws.mediachan.c` | 810 | Catalog management, subscription operations, and binary delivery. |

## Long-function queue

The split units now satisfy the function-size target. Other inventoried
functions above 300 code lines still need separate reviews; splitting these
requires understanding command behavior, DSP state or input dispatch, rather
than only moving a definition to a new file.

| Function | Code lines | Location |
| --- | ---: | --- |
| `ws_handle_chat_msg()` | 598 | `librrprotocol/srv.chat.c:1098` |
| `run_loop()` | 410 | `fwdsp/fwdsp.c:807` |
| `main()` | 510 | `rrclient/tests/transport_urls.c:192` |
| `stdin_ev_cb()` | 363 | `librustyaxe/tui.keys.c:354` |
| `client_cmd_completions()` | 323 | `rrclient/cmd.completion.c:232` |
| `main()` | 389 | `rrclient/tests/codec_commands.c:171` |
| `main()` | 322 | `rrclient/tests/irc_events.c:199` |
| `qrz_parse_http_data()` | 303 | `callsign-lookup/src/qrz-xml.c:47` |
| `main()` | 373 | `rrserver/tests/database.c:88` |
| `ws_handle_rigctl_msg()` | 326 | `librrprotocol/srv.rigctl.c:184` |

## Findings to review with the maintainer

These behaviors were observed while reading the affected code. They are
intentionally unchanged in this refactor. Priority describes what to examine
next, not an assertion of an exploitable vulnerability.

1. **CPU, high:** `rrserver_relay_poll()` calls `rr_relay_local_users()` on each
   main-loop pass whenever the link node is available. That allocates/builds a
   dictionary, walks active connections and all user slots, then compares the
   result with the prior directory even when no membership changed. Candidate:
   dirty-generation tracking on connect/auth/disconnect/account changes, with
   a bounded periodic reconciliation. Check multi-session counts and linked
   user-list propagation before replacing the current behavior.
2. **Memory/CPU, high:** `WebUiChat.Append()` appends directly to the stored HTML
   for an inactive room and returns before applying scrollback limits. A busy
   background room can therefore grow without the active-room cap. Enforce a
   per-room bounded message store, preserving room-switch scrollback and links.
3. **CPU, medium:** the same method reads/copies the complete active-room
   `output.html()` after every message, reparses it on switching rooms, and
   schedules a scroll timer for each append. Candidate: bounded per-room message
   records and batched DOM/scroll updates. Measure on the Pi/browser first.
4. **CPU/bandwidth, medium:** `media_packet()` base64-encodes each 4000-byte relay
   chunk; `rr_relay_packet()` JSON-encodes the payload into a string inside a
   second JSON envelope. This creates allocations, escaping and expansion for
   media traffic. A binary link-media design would be a protocol change and
   needs separate compatibility/backpressure work; do not remove validation.
5. **Security hardening, review:** `rrserver_handle_nomatch()` logs the complete
   unmatched event payload; the hello handler dumps its complete dictionary.
   Arbitrary fields in unexpected/misrouted payloads can reach diagnostics.
   Review redaction/allowlisting before assuming passwords or tokens cannot be
   logged. No confirmed normal-path credential disclosure was established here.
6. **Security hardening, review:** `user_temp_password()` uses a cryptographic
   random source but only eight decimal digits, and account creation/reset
   allows a seven-day lifetime. Review entropy, expiry and effective online
   attempt limits together. This is not a claim that the PRNG is weak.

## Validation and limits

- Production radio-profile server build and all changed standalone source lists.
- Database schema/identity/account/audit/quota tests; room confirmation expiry
  and session binding; bridge privilege gates; relay proxy-TX liveness/key-up.
- Real local multi-server link tests with dedicated, shared TLS and shared plain
  listeners: discovery, remote control, administration, shared subscriptions,
  permissions and reconnect/restart.
- Server media channel provisioning, object protocol and multi-rig fixtures.
  Their older control-only fixtures also needed current bridge dependencies and
  explicit no-media adapters in order to link.
- Complete browser and shared-selftest suites, including the source-size cap.
- Relay sources pass a syntax compile with Mongoose disabled.
- Tokenized function-body comparison against the safety checkpoint preserves
  all original database/event/relay bodies except event registration delegated
  to domain registrars and the explicit no-Mongoose visitor guard.

An exploratory server compile with SQLite disabled fails in existing replay,
authdb and quota paths. The checkpoint sources fail the same experiment; this
pre-existing profile limitation is outside the organization change. It is not
a regression attributed to the new files. GTK/TUI client source is unchanged;
this work does not make a claim about existing GTK audio stuttering.

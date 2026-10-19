# Known issues and RC1 follow-up

Tracked from hands-on client/server testing. **Open** means observed or requested, not confirmed fixed. Priorities are triage suggestions; they do not determine whether a release may ship. Keep this list concise and update it as fixes are verified.

## High priority: correctness and topology

| Area | Issue | Expected result | Status |
| --- | --- | --- | --- |
| KiwiSDR / rrserver | Connecting to KiwiSDR creates an unusable `#sdr-*` room and a working `#rplywv00-rig1` room. This falsely implies the temporary external SDR is owned by station `rplywv00`. | One functional room for the external receiver, with correct resource identity, ownership and human-readable topology; no phantom local rig room. Check registration, room mapping, cleanup, UI discovery and reconnection. | Open |

The SDR problem needs a server-side root-cause fix, not merely a room rename or a client-side hide operation. Test connecting, switching VFOs, multiple receivers, disconnecting, restarting, and the resource tree after cleanup.

## Client behavior and usability

| Area | Issue | Expected result | Status |
| --- | --- | --- | --- |
| rrclient config editor | If `~/.config/rrclient.cfg` does not exist, opening the editor currently skips writing the active settings there. | First save a snapshot of the in-memory configuration to that path (creating the directory as needed), then open the editor. Preserve permissions and do not overwrite an existing file. | Open |
| GTK dialogs | Confirm Quit may be obscured by another always-on-top/modal dialog. | Confirm Quit should be presented above the relevant client dialogs and remain accessible; use correct GTK transient/modal/window-group behavior rather than relying only on an always-on-top hint. | Open |
| TUI prompt | Prompt repeats information already in the status line. | Display a simple `>` prompt, with status kept directly above it. | Open |
| SDR Directory | Tab appears to wait for receiver details before presenting loading feedback. | Show the tab immediately with a clear loading state, then populate results or show an actionable error/empty state. | Open |
| Status tab | Connection state and user count do not stand out sufficiently. | Show `CONNECTED` and `Users` in red when connected, while maintaining accessible text labels (not color alone). | Open |

## Admin: non-user connections

**Requested enhancement (not yet implemented):** Add an **Admin → Connections** subtab for outbound and inbound non-user sessions: temporary SDR receivers, server links, and other bridge/service connections. Add a way to open this view directly from **SDR Directory**.

The connections list should show type, peer/target, state, owning server/node, associated rooms and rigs/VFOs, age, traffic or errors where available. A selected row should expose as much relevant diagnostic detail as the server can safely report, plus authorized actions such as **Disconnect**. Confirm destructive actions and enforce authorization server-side; do not present a user session as a hardware/bridge connection. Start by reusing existing connection and bridge state, then extend protocol reporting only where needed.

## Verification guidance

Before marking these resolved, test an updated rrserver **and** rrclient together, then test regression behavior with multiple connections. In particular:

- The SDR room fix must survive reconnect and server restart without reintroducing local `-rigN` identities.
- Closing modal dialogs must not leave Confirm Quit inaccessible.
- The initial config snapshot must represent the actual active in-memory values, not merely a copied example file.
- New connection-management actions must reject unauthorized users.

For changes to release scope, see [release checklist](release-checklist.md). Bug reports should include the exact versions, steps to reproduce, relevant logs, and whether a linked server is involved.

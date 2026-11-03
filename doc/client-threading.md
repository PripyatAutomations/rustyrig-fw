# Client frontend threading — revised direction

## First end-to-end event migration

Confirmed PTT state now emits `client.ui.ptt.state` from the core; the
GTK frontend subscribes on the named `gtk` domain, receiving an owned
one-byte state snapshot. The GTK implementation no longer needs a direct
core-thread callback for this notification. Non-GTK frontend behavior stays
on the compatibility path.

This is deliberately one notification, **not** a threaded GTK rollout.
Other `frontend_ops` calls and GTK-to-core commands still access shared
state. The GTK domain currently runs on the shared default loop; separating
it is blocked until all direct calls and synchronous queries are addressed.

## Branch status (v1.1/frontend-threading)

The first implementation milestone is in this branch:

- `rrclient/thread.dispatch.[ch]`: one-shot, owned-payload GLib dispatch
  to an explicit context, with closed-queue rejection and cleanup.
- `rrclient/rules.mk`: links the dispatcher into the client core.
- `rrclient/tests/test_thread_dispatch.sh`: tests delivery and rejection.

**Not enabled yet:** GTK-thread startup and frontend event migration. The
current client still uses the original shared default context; no unsafe
half-migration of network polling has been attempted. Build and GTK runtime
verification are still required. The obsolete `core.loop` experiment is
not used for this topology.

## Preferred Linux/GTK3 topology

**Core owns the process main thread and default GLib context**, identical
to the current TUI transport scheduler. **GTK owns a dedicated frontend
thread**, with all `gtk_init`, widget creation, `gtk_main`, signal
handling, and destruction on that same GTK thread. Audio workers remain
independent.

GTK3 is not thread-safe. A worker-thread GTK main loop may work on the
Linux X11/Wayland targets but must be verified against GDK/display backend,
GLib default-context source attachment, and any libraries requiring process
main-thread UI. Do not presume this layout works on all platforms.

**Critical blocker:** merely running `frontend_ops()->init/run` on another
thread is unsafe. Core calls frontend ops directly and UI event handlers
directly access mutable connection/media state. Each crossing must be
marshaled. `g_timeout_add` and `g_idle_add` attach to the global default
context, so GTK timers must be attached explicitly to the GTK-owned context.
Synchronous frontend ops (such as `confirm_dialog` and
`chat_current_room`) need asynchronous replacement or a narrow safe
request/reply protocol without blocking network scheduling.

The currently added `rrclient/core.loop.[ch]` is **scaffolding only**
(not called from startup). It must not be activated for network polling
while the above cross-thread calls exist. It can be repurposed into a
frontend dispatch abstraction once contracts and ownership are audited.

## Safe migration

1. Inventory all frontend ops, GTK event callbacks, and default-context
   sources. Split async notifications from synchronous queries.
2. Introduce GTK-thread queue/dispatch with **owned immutable payloads**,
   plus core-command queue delivered on the default core context.
3. Ensure GTK initialization, event-source attachment, and destruction
   happen consistently on GTK owner thread.
4. Start core loop on the main thread; start GTK thread only after both
   queues and lifecycle handling are ready.
5. Compare audio/RTP timing against TUI while exercising multihome, tabs,
   shutdown, dialogs, disconnect/reconnect, and event bus callbacks.

---

# rrclient execution domains

## Goal

Keep GTK fully independent of client network and media control scheduling.
GTK's main loop stays on the initial process thread. The client core runs
on a dedicated GMainContext/GMainLoop thread. Audio workers and fwdsp remain
independent. TUI uses the same core facilities without requiring GTK.

## Current coupling (31 October 2026)

- `rrclient/main.c` installs `ws_poll_cb` with `g_timeout_add[_full]`
  into the default GLib context.
- `rrclient/gtk/gtk.core.c` owns `gtk_main()`, also on the default context.
- `rrclient/media.direct.c` dispatches received RTP frames using
  `rrclient_connection_dispatch()`, which mutates the active connection
  context. RTP attachment/channel state must not be concurrently touched.
- `rrclient/audio.c` currently owns audio subscription, decoder management,
  and worker coordination; its callback ownership needs auditing.
- Logging callbacks that touch GTK must execute on GTK's context.

## Ownership contract

- **GTK thread:** GTK/GDK widgets and signals, frontend presentation state,
  user interaction, all GTK-bound logging subscribers. No socket polling,
  decoder startup, network encryption, or subprocess maintenance.
- **Core thread:** `rrconn_t`, active connection/selection context,
  media subscription state, RTP attachments, network transport, reconnect
  timers, protocol events, and process supervision. Its own
  `GMainContext` and `GMainLoop`, not the default GTK context.
- **Media thread(s):** bounded audio ingress/processing with stable
  channel identity; no GTK calls and no mutation of `rrconn_t`.
- **Logger:** queued submission; emit GTK-affecting callbacks on GTK owner
  (not arbitrarily on core or logger thread).

## Boundary design

`rrclient_core_post(command, payload)` must enqueue an owned immutable
command; no caller-owned stack pointers cross the boundary.
`rrclient_frontend_post(event, payload)` similarly enqueues immutable
presentation snapshots, invoking GTK updates on GTK's main context.
A core command should carry request id + stable connection UUID/generation,
not a raw `rrconn_t *` whose lifetime can end before dispatch.
A frontend command must not synchronously block waiting for core replies.
State snapshots can coalesce frequently updated VFO meters/waterfall
notifications. Never coalesce PTT changes or audit/security events.

Core thread initialization should push its context as thread-default and
attach the core network/reconnect/audio-supervision sources explicitly to
that context, then run its loop. `g_timeout_add()` always uses the global
default context; replace it with `g_timeout_source_new()` +
`g_source_attach(core_context)` for moved sources. GTK-only sources stay
on the default context. The transport event callbacks and their dependent
state must all migrate together.

## Migration and verification

1. Inventory timers and core callbacks; tag each GTK/core/audio ownership.
2. Add explicit dispatch APIs with owned payloads and shutdown generation.
3. Move network polling and connection protocol state together to the
   new core context, while GTK remains on the initial/default context.
4. Replace direct UI->network calls with commands and network->UI updates
   with marshaled events. Check multi-homed connection selection semantics.
5. Keep media ingress buffered independently, then move RTP decrypt,
   decoder delivery, and audio stream tracking behind stable handles.
6. Shutdown: stop the core loop and wait for it to return before the GTK
   owner thread removes widgets, event tokens, and the GTK event domain.
   Release the event context only after listener removal is verified.
7. Compare RTP ingress backlog, dispatch gaps, PCM starvation, UI
   responsiveness, CPU overhead, and thread sanitizer reports.

## Do not

- Call GTK from the core thread.
- Move only `ws_poll_cb` while leaving connection state/IRC callbacks
  accessible from GTK.
- Run `gtk_main()` on the worker as a shortcut.
- Use `g_idle_add()` as a core-thread dispatch primitive: it attaches
  to the default context.
- Hold a lock over network processing, codec startup, or GUI callbacks.
- Treat `CAP_SYS_NICE` or realtime priorities as a substitute for
  isolation.

## Connection status event migration

`rrclient_update_connection_ui()` emits `client.ui.connection.state` with a copied
integer snapshot for the GTK frontend. The GTK domain subscriber updates the
connection button on the destination context. Other frontends retain the
existing callback. VFO state still uses direct frontend callbacks: its GTK
handler currently reads selected server and active room from core state,
so it must be refactored to a complete snapshot before thread isolation.

## Owned VFO snapshots

`vfo.c` now sends `client.ui.vfo.snapshot` to GTK with copied VFO ID,
mode, server, room, frequency, width, power, PTT and availability fields.
GTK's paint/flush callback consumes those values rather than querying the
core's active room, selected server, VFO selection or availability.
The frontend's existing `vfo_state` callback remains for ABI compatibility
but is bypassed for the current GTK sender path.

**Not yet threaded:** GTK still has many core reads beyond VFO painting,
including UI interactions and synchronous frontend operation callbacks.
`g_idle_add` and other GTK sources must be attached to the eventual
GTK-owned context, not the core/default context; the current domain context
is still captured from the default thread. Module callback lifetimes and
pending dispatch cleanup must be audited before activation.

## Experimental GTK worker (diagnostic only)

Set `RRCLIENT_GTK_THREAD_EXPERIMENT=1` to launch GTK init/run on a
GLib worker while the process main thread runs a core `GMainLoop`.
The normal configuration is unchanged. This is **not yet safe for regular
operation or transmitting**: GTK3 uses global/default-context sources and
many unconverted direct frontend/core calls, so two threads may service
callbacks intended for different owners. This mode is intended to expose
the next migration requirements and must not be promoted until thread
ownership is verifiably isolated. A failed GTK initialization is joined
before unloading its module. GTK's worker must finish before module unload.

To complete real isolation we must migrate GTK's default-context sources
(or make the core use a separate context, explicitly attaching all core
sources), replace all remaining synchronous cross-thread UI invocations,
and verify no GTK callbacks execute on the core thread. Only then test
audio starvation with simulated slow rendering.

## Experimental thread diagnostics

The opt-in threaded GTK experiment now logs GTK and core GThread identities,
checks GTK-domain event callback ownership, and logs core network scheduler
poll gaps exceeding 100 ms (rate-limited to 10 seconds). Look for
`gtk.thread`, `core.thread`, and `core.scheduler` diagnostics. The
`RRCLIENT_GTK_THREAD_EXPERIMENT=1` mode is still **not safe for transmission**:
legacy GTK idle/timeout sources and synchronous core calls need isolation.

## Split core polling context (experimental)

With `RRCLIENT_GTK_THREAD_EXPERIMENT=1`, the core now owns a separate
`GMainContext`. The native/WebSocket polling timer, optional Mongoose
sweep, fwdsp maintenance timeout, and the named `core` event domain are
attached explicitly to it; the core main loop runs that context on the
process main thread. GTK still runs its legacy default context on its worker.
This is **partial isolation**, not a proof of safety: other core sources
(e.g. additional GLib watchers, signals, and legacy callbacks) may still
attach to the global default and be dispatched by GTK. Avoid TX until the
remaining cross-thread API calls and source ownership are audited. Check
`core.scheduler` thread IDs against `core.thread` again after rebuilding.

## GTK text-view crash investigation (November 1)

A heap double-free was detected by Pango while GTK validated a TextView
layout. This is consistent with, but does not prove, cross-thread widget
mutation. The GTK client and host log callbacks previously called
`gtk_logview_append()` directly from logger/protocol threads. Those paths
now copy complete log records and queue GLib idle delivery on GTK's default
context instead. This removes one known cross-thread TextView mutation.

This is **not yet a complete unload-safe or GUI-thread-safe solution**:
other GTK callbacks still access core state, and pending idle callbacks
must be explicitly drained/cancelled on module teardown. Test in the opt-in
threaded mode without TX, under ASan/TSan where practical. Capture all
threads with `thread apply all bt` on another crash.

## GTK chat buffer cross-thread fix

`frontend_gtk_vprint` previously called `ui_print_gtk` directly from
core/transport callers and could mutate GtkTextBuffer during GTK layout.
It now snapshots formatted text and delivers it via the GTK event context.
This follows the prior GTK LogView marshaling fix. Remaining GTK
frontend_ops callbacks must still be audited; these changes do not make
the experimental thread split universally safe. Avoid TX testing.

## Confirmed GTK widget mutation from core thread

GDB thread dump (Nov 1) showed the process main thread handling a network
`join` event and calling `frontend_gtk_chat_set_authoritative_room()` →
`gtk_chat_tab_add()` → `gtk_notebook_set_current_page()`, while the GTK
worker was measuring the same widget hierarchy. All seven void chat widget
callbacks (add/remove/topic/show status/authoritative/query/VFO change) now
copy their parameters and enqueue work onto GTK's context. The synchronous
`chat_current_room` getter still needs conversion to core-owned state;
other frontend_ops calls and shutdown teardown remain unisolated. The
experimental mode is not yet TX-safe.

## Reusing JOIN through the event bus

The authoritative room selection after a network JOIN now publishes
`client.ui.chat.authoritative-room` using an owned NUL-terminated room-name
payload. GTK subscribes on its `gtk` delivery domain and changes notebook
selection on the owning GUI thread. TUI retains its existing direct path.
This begins replacing the temporary chat command queue with native event-bus
subscriptions. Full JOIN/room/roster state still needs a versioned owned
snapshot before removing the other legacy frontend callbacks.

## Clicking lobby rig-room links

GTK's `gtk.chat.links.c` click handler previously called
`rrclient_connection_select()` and `cmd_join()` directly on the GTK thread.
That let core networking and resulting synchronous legacy event handlers
reenter GTK unpredictably during widget layout. The handler now publishes
`client.ui.chat.link-join` with a copied pointer-free server/room payload;
the core domain processes connection selection and JOIN on the core thread.
Other GUI-to-core direct calls require the same command/event migration.

## Original protocol events across domains (Nov 1)

librustyaxe now exposes `event_on_domain_token(event, cb, user, domain)`
for existing text protocol events, without introducing synthetic UI events.
The dispatcher copies event names and JSON strings and passes a NULL
`rrconn_t *` to the receiving thread (never borrow a live connection).
GTK now subscribes directly to `talk.msg` on its `gtk` domain and
creates/marks chat tabs there, rather than invoking `gtk_chat_incoming`
from the synchronous core message handler. This addresses the observed
replay-driven concurrent creation of a lobby chat tab.

The existing GTK handler still references shared login and connection
information, and the other legacy frontend operations need migration.
Legacy dispatched-listener teardown, domain unregister and outstanding
callbacks require further lifetime hardening before dynamic module unload.

## Protocol-event presentation migration (Nov 1 follow-up)

GTK now receives the existing `join`, `part`, `room.deleted`,
`room.topic`, `room.vfo`, `room.vfo-list`, `room.state` and
`userinfo` on the `gtk` event domain. The core event handlers retain
room/session and roster authority while suppressing corresponding direct
frontend calls to GTK. `talk.msg` already uses a GTK domain listener.
The PART handler only closes a tab when its authenticated session token
matches, and preserves the tab after a kick. The frontend is still not
completely thread-isolated: other synchronous `frontend_ops` calls,
shared core state read by GTK, and queued listener teardown remain to audit.
Do not enable TX in the experimental split.

## Resource Explorer and Remote Files boundary work (Nov 2)

The configured GTK topology now starts the GTK module on its owner thread
and dispatches core events through the GTK context when
`core.new-threading=true`; setting it to `false` retains the legacy shared
loop. The audio pipeline was not changed. The split is **not yet release-safe
for TX**: the outstanding paths below still access core-owned state from
GTK or have callback lifetime assumptions that need explicit verification.

Resource Explorer inventory rows now carry copied room, object, media and
serial presentation fields, and native IRC rows/summaries are collected on
core before GTK updates its models. Refresh/action requests, rig-property
reads and menu writes use owned events; GTK stores property responses by
profile/UUID/property and correlates them by request ID. The GTK event
delivery bridge now asserts its owner thread in debug builds. The PTT
hotkey wrapper reports a queued action as handled so core does not issue a
duplicate command.

Remote Files now receives an owned list of authenticated server profiles
and sends listing, mutation and transfer commands through the core event
domain. GTK no longer resolves connections or calls file-transfer/network
APIs directly. Results return as copied status/completion events. Native
transfer tests continue to exercise upload/download and remote editing.

Administration tables, access requests and controls now submit copied
profile-scoped command events instead of resolving connections or sending
WebSocket messages from GTK. The Controls page also receives its connected
server list as correlated profile snapshot events. Rejection status uses
weak widget references, and the Controls page unregisters its event
subscriptions on destruction. These changes do not cover every callback
lifetime in the Administration window or make the remaining client paths
safe for the threaded mode.

### Remaining unsafe or incomplete paths

- `rrclient/gtk/gtk.userlist.c` still traverses `global_userlist`, reads room,
  media and object state, resolves connections, and sends WebSocket commands
  from GTK callbacks. GTK roster and VFO presentations need complete owned
  snapshots and command events.
- Resource Explorer's local serial scan still calls `rr_serial_foreach()`,
  `rr_sercom_info()` and `rr_serial_find()` from GTK. Device/configuration
  state and local attach/disconnect actions need core snapshots/commands.
  `gtk.serial.c` has an event-based remote attach path, but local settings
  and cancellation/teardown still need review.
- Other GTK controls (including chat, PTT, volume, TX power, codec and
  terminal paths) still contain direct reads or writes of connection/media
  state. Their callbacks and sources must be audited before declaring the
  split safe.
- The owner assertion currently guards the GTK event-delivery bridge and
  GTK init/run entry points; it is not a blanket assertion around every GTK
  API call. Event-listener unregister, queued callback draining, and module
  unload ordering also remain to be verified.

`gtk_resources` now tests copied property-request/result payloads and the
serial attach-request event shape. Its legacy interactive serial-settings
dialog automation is not covered; the prior fixture no longer matched the
dialog's current widget structure. Do not treat this partial migration as
completion of the v1.1 thread separation.

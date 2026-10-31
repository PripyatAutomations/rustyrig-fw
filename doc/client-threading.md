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
6. Shutdown: stop accepting commands, stop/close core transports, join
   workers, drain queues, then release UI and logger resources.
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

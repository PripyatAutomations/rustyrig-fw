# Focused safety and diagnostics audit — 2026-11-06

Scope: recent GTK floating-panel scaling, RX routing, local Kiwi/rrbridge
subprocesses, and telemetry. This is a **source review**, not a project-wide
memory-safety certification or runtime sanitizer pass.

## Fixed in main

1. **Potential use-after-free on panel teardown / reparenting**:
   `gtk.scale-container.c` installed descendant `add` signal handlers with
   raw pointers into wrapper-owned zoom state. Changed to object-bound signal
   connections, which disconnect automatically when the wrapper is finalized.
   CSS providers are now replaced when a widget moves between panels.
2. **Telemetry induced receiver disconnects**:
   `rrbridge/main.c` treated failures to transmit optional statistics
   (Kiwi traffic and _rr_stats) as fatal IPC errors. Optional reports are now
   best-effort. Critical status, state, control and media send failures retain
   strict handling.
3. **Poor child termination diagnostics**:
   `rrclient/local.bridge.c` reported a raw wait status and generic
   `control channel closed or failed`. Child exit code vs termination signal
   (and core-dump indication, where available) is now decoded, with PID/fd/
   errno context for IPC read failures. An EOF may still appear before the
   GLib child-watch exit report.
4. **Incompatible event-struct aliasing**:
   `gtk.floating-panel.c` cast `GdkEventButton *` to `GdkEventMotion *`
   during resize release. Both now pass coordinates to one shared geometry
   function without type punning.

## Follow-up: GTK/core ownership and process shutdown

A second review of `rrclient/local.bridge.c`, `rrserver/bridge.manager.c`,
Resource Explorer, and the GTK frontend shutdown sequence found:

- **Fixed:** Resource Explorer's zebra-row rebuild source was left queued when
  its backing browser was destroyed. The browser's destroy callback now
  cancels the source.
- **Fixed:** A scroll-restore idle used the global `browser` pointer without
  carrying the identity of the browser that requested it. It is now coalesced,
  owned by the requesting browser, and cancelled on destroy. This prevents
  a stale callback from changing a newly recreated explorer.
- **Reviewed, no confirmed double-free:** The local bridge's normal stop path
  removes core poll/kill timers and retains the allocation until the GLib
  child-watch handler reaps the process. This remains a useful target for
  ASan under rapid reconnect/shutdown, but should not be rewritten without
  reproducing a defect.
- **Outstanding:** The GTK domain is bound to its owning context, but several
  frontend callbacks still need a complete snapshot audit to establish that
  no core-owned pointers are accessed on GTK. See `doc/client-threading.md`.
  No claim of full thread isolation is made here.

## Further priority audits

- **High: GTK lifecycle**: inspect all event subscriptions, timers, signal
  handlers, and GTK/core thread boundaries for callbacks targeting destroyed
  widgets; repeat panel creation/destruction with ASan enabled.
- **High: bridge shutdown**: check child-watch ordering vs IPC/media polling
  and reconnects; verify errors and crashes always report exit reason, and
  parent only reaps its own subprocesses.
- **High: IPC validation**: fuzz metadata, frame lengths, counts, string
  termination, and malformed/partial `SOCK_SEQPACKET` reads in both directions.
- **Medium: per-panel zoom**: test widget reparenting, new dynamic children,
  extreme zoom, custom Cairo canvases, native popovers, and session teardown.
  It uses native widget geometry, not a generalized affine event transform.
- **Medium: logging**: preserve original `errno` before cleanup; attach
  subsystem, process identity, transport stage, and actionable failure reason.
  Avoid flooding logs from retryable network failures.
- **Medium: telemetry**: distinguish malformed records, dropped optional
  samples, and stale counters. Avoid treating message success as delivery of
  the payload to the UI.

## Suggested validation

Compile with `-Wall -Wextra -Wpedantic` and run targeted ASan/UBSan builds
(`-fsanitize=address,undefined -fno-omit-frame-pointer`). Repeat 50–100
connect/disconnect cycles on local Kiwi, exercise DNS errors, open/close
floating panels while new streams arrive, and zoom through the full range
during active callbacks. Use Valgrind or GLib fatal-warnings mode to
cross-check lifecycle problems. Treat sanitizer findings as separate verified
bugs, not evidence of correctness merely because the build succeeds.

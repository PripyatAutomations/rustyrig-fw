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

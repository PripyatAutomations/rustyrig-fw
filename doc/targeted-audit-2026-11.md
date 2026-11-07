# Focused safety and performance audit — 2026-11-06

Scope: current `main` source, including client GTK/core, local receiver
directory, mesh route validation, and selected hot paths. This is a **targeted
source review**, not an exhaustive security review or a measured performance
profile. Build, sanitizer, and live mesh tests have **not** yet been run here.

## Fixes in this pass

- **GTK/core database scheduling:** `receiver.directory.c` attached the
  network completion timer and queued learned metadata to GLib's default
  context. In the threaded GTK topology this can process core-owned SQLite
  state on the GTK loop. Attach both sources to `rrclient_core_context()`.
  Standalone directory test fixtures now provide a default-context stub.
- **Window timer lifetime:** `gtk.winmgr.c` used a delayed callback with a
  borrowed `GtkWidget *`. Retain/release the widget around the source, and
  remove (not repeat) the source if the tracked window has disappeared.
- **Abandoned geometry experiment:** remove disabled `#if 0` frame-extents
  logic from `gtk.winmgr.c`; preserve the working position API.
- **Mesh route input:** `link.mesh.c` checked only `strlen(id)==36`.
  Validate canonical lowercase UUIDs before inserting IDs into route state.
  The authoritative `link.tree.c` already performs this check. Add
  malformed-identifier regression assertions.

## High-priority outstanding risks

1. **AF spectrum GTK/core ownership.** `gtk.spectrum.c:update()` calls
   `audio_rx_streams()` every 250 ms while visible. That function traverses
   `rx_routes`, which is mutated and freed on the core thread by
   `audio_sync_rx_channels()`. `audio.h` explicitly marks that inventory
   as core-loop-only. Replace GTK direct reads with a *core-produced immutable
   snapshot* delivered to GTK, not a GTK-side mutex around the route list.
   Verify rapidly switching RX codec to NONE while AF waterfall is open.
2. **RX decoder callback lifetime.** `audio_sync_rx_channels()` registers
   `client_channel_pcm` with an `rx_route_t *` user pointer and later
   destroys the route when a subscription disappears. Verify that
   `fwdsp_codec_stop_channel_immediate()` synchronously prevents all pending
   callbacks, including callbacks already queued on another thread, before
   freeing the route. Consider a generation/refcount or queued owner-side
   teardown if not guaranteed.
3. **Webcam state ownership.** `gtk.webcam.c` protects its pending-frame
   `GBytes` with a mutex but reads `webcam_connection` and visibility in
   several other callbacks without a single consistent ownership contract.
   Review disconnect/reconnect and decoder startup on the core thread.
4. **Transport trust and resources.** Re-audit certificate verification,
   bounded send queues, and per-peer resource quotas under actual native TLS
   deployment. The October audit in `doc/transport-security-audit.md`
   explicitly deferred some trust and abuse controls. Do not assume these
   are solved by route-ID validation.

## Memory and CPU follow-ups

- **SDL/GTK spectrum:** AF view wakes at 4 Hz when hidden/disabled; consider
  pausing its timer when the view is unmapped and rearming on map. Keep the
  decoder/audio engine entirely independent.
- **RF waterfall:** the renderer creates a fresh Cairo RGB24 surface for each
  paint of 1024 x up-to-32 bins. Reuse a cached surface keyed by history depth
  after profiling; ensure resize/color-palette changes invalidate it.
- **Log/tree models:** inspect GUI log and resource Explorer append/rebuild
  behavior during heavy multi-node churn. The log store has a default 1,000-row
  limit, but removing the first GtkListStore entry at each append can still
  shift rows. Prefer batching/coalescing only if profiling supports it.
- **Directory refresh:** 50-ms job-completion polling now runs on the core
  context (correct owner), but a completion notification would eliminate
  wakeups while fetching. Maintain the current bounded body/row limits.
- **Mesh routing:** the adjacency matrix is bounded; Dijkstra is quadratic.
  No premature rewrite is warranted without real cluster-size measurements.

## Verification sequence

1. Compile the client/server and standalone directory/mesh tests; check that
   the default-context test stubs link and GLib sources are destroyed on exit.
2. Run `rrclient/tests/receiver_directory.c` and
   `rrclient/tests/gtk_receiver_directory.c` plus
   `rrserver/tests/link_mesh.c`.
3. Run client with ASan+UBSan and switch active RX codecs to NONE during
   stream arrival/reconnect; exercise the AF spectrum and webcam panels.
4. Profile idle, busy RF/AF waterfalls, 100+ chat lines/second and full
   resource Explorer inventory with `perf`, heaptrack or equivalent.
5. Fuzz native mesh node identifiers, malformed resource metadata and
   unauthorized relayed operations on a disposable test cluster.

Avoid deleting broad legacy transport code or changing authorization
semantics before tracing each call site and running regression tests.

# Subprocess telemetry (initial version)

RustyRig keeps rig control and network state in the host process while media
and external SDR drivers run in independent children. Process statistics must
not block the media or rig control loops.

## Version 1 counters

* `fwdsp` emits an `RRSTAT1` line at most once a second on its supervised
  stderr pipe. Values are cumulative **payload** bytes and frame counts
  consumed by appsrc / produced by appsink, along with GStreamer warning/error
  counts. The manager consumes these records without forwarding them to the
  application log. The core can read `fwdsp_stats_snapshot()`.
* `rrbridge` emits one private `_rr_stats` metadata IPC message with each
  one-second heartbeat. Values are cumulative IPC message counts, PCM bytes
  and frames delivered to the host, and bridge IPC errors. Client and server
  supervisors consume these privately through
  `rrclient_local_bridge_stats_snapshot()` and
  `rrserver_bridges_stats_snapshot()`. They are **not** receiver catalog
  properties or persisted receiver metadata.
* The Kiwi driver separately reports `traffic.rx` and `traffic.tx` as
  cumulative remote socket bytes, including waterfall and audio WebSockets.
  These are kept separate from PCM/IPC metrics and from RustyRig WS/RTP
  transport meters.

All snapshots are **core-owned**. The GTK frontend must request copies
through the normal core-to-GTK event boundary. Do not read live process
descriptors or the manager's registry from GTK.

## Semantics and failure handling

- Counters are cumulative per child process. The host resets per-process
  counters on respawn; GUI rates must use differences between snapshots of the
  **same PID** and must handle counter reset or missing samples.
- `last_report_us` uses the host's monotonic clock. Staleness is measurable
  independently of subprocess liveness.
- Telemetry delivery is best effort. A failed statistics write must not
  terminate or stall the audio pipeline.
- The fwdsp stderr metrics channel is interim; production protocol evolution
  should use a dedicated low-priority, versioned control channel, independent
  of PCM stdout and the critical child control socket.
- Do not count client-to-bridge PCM IPC as additional network traffic or add
  RTP rates to WebSocket counters; these are different measurements.

## Planned additions

Add separate input/output error and drop counters, queue depth/high-water
marks, playback underruns, late processing, pipeline state transitions,
restart reason/count, per-worker CPU time and RSS (sampled by supervisors),
latency percentiles, and error-category counts. Provide on-demand diagnostics
and bounded periodic summaries; avoid an unconditional per-second log line
for every child.

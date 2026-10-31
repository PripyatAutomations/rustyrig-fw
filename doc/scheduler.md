# Managed worker scheduling (initial integration)

Scheduling is provided by `librustyaxe/scheduler.h` and applied when
`libfwdspmgr` launches fwdsp and when rrserver/rrclient launch rrbridge.

## Configuration

Put these entries in the normal RustyRig configuration (flat dotted keys):

```ini
[general]
scheduler.fwdsp.profile=normal
scheduler.rrbridge.profile=normal
```

The accepted profiles are:
- `normal`: SCHED_OTHER, preserve inherited nice.
- `background`: SCHED_BATCH and nice +5.
- `idle`: SCHED_IDLE and nice +10.
- `interactive`: SCHED_OTHER and nice -2; may require privilege.
- `realtime-audio`: SCHED_RR with RT priority 10; requires appropriate
  RLIMIT_RTPRIO/CAP_SYS_NICE, should only be enabled deliberately.

Each launched child logs its requested profile and the effective scheduling
class, RT priority and nice value. Failed policy changes log an OS error
rather than aborting the worker. An application that itself runs at elevated
priority should use an explicit `normal` profile for managed non-RT children.

## Safety and next work

This first integration changes policy from the parent *immediately after spawn*.
There is a race before the policy is applied, and RT threads may already have
started. A future `subproc_spawn` pre-exec scheduling hook (with explicit
error reporting), or a controlled child startup handshake, should close it.
The library scheduler API is per-task, not process-group-wide: threads created
before applying a policy need separate handling. This also means `fwdsp`
can later give different priorities to specific audio threads.

Do not make GUI, GStreamer pipeline or arbitrary plugin threads RT by
default: priority inversion, blocking I/O and unchecked queues can destabilize
the host. RT privilege should be delegated to only audited, bounded workers.

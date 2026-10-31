# Managed worker scheduling (initial integration)

Scheduling is provided by `librustyaxe/scheduler.h` and applied when
`libfwdspmgr` launches fwdsp and when rrserver/rrclient launch rrbridge.

## Configuration

Put these entries in the normal RustyRig configuration (flat dotted keys):

```ini
[general]
scheduler.fwdsp.device.profile=normal
scheduler.fwdsp.codec.profile=normal
scheduler.fwdsp.processor.profile=normal
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

Scheduling is applied in the forked child before exec to avoid accidental
inheritance of RT policy into worker startup. Invalid or unauthorized profiles
cause the child to exit 126 with a diagnostic on stderr. Spawn success only
means the child was created, not that exec/profile application succeeded;
the supervisor should observe child exit/status.
The library scheduler API is per-task, not process-group-wide: threads created
before applying a policy need separate handling. This also means `fwdsp`
can later give different priorities to specific audio threads.

Do not make GUI, GStreamer pipeline or arbitrary plugin threads RT by
default: priority inversion, blocking I/O and unchecked queues can destabilize
the host. RT privilege should be delegated to only audited, bounded workers.

## fwdsp role assignment

- `device`: `FWDSP_ROLE_PROCESSOR` with capture or playback I/O, e.g. physical audio devices.
- `processor`: other `FWDSP_ROLE_PROCESSOR` stages such as resamplers and generic transforms.
- `codec`: other fwdsp codec/translator workers. These should not inherit RT by default.

All default to `normal`, including audio devices. Explicit `realtime-audio` is available only after measuring bounded callback behavior and providing OS permissions. Per-thread RT selection inside fwdsp is future work; promoting the entire process changes only the calling task's scheduling policy on Linux, not all later worker threads.

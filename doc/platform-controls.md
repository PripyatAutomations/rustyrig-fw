# Exported node controls (v1)

Hardware-facing, user-adjustable settings should be **typed properties of their
owning object**, not one-off chat commands or pretend rigs.

## Node controls

`rrserver/platform.controls.h` provides a bounded registry with
`rrserver_platform_control_register()`, get/set, and enumeration APIs.
Providers register stable names and server-lifetime callbacks. A provider
supplies label, unit, permission token, range, step, and current value.
The registered device and handler are **server-local**; clients transmit
only a control name and a typed value, never a filesystem path.

The initial provider is `display.brightness` backed by Linux sysfs.
No backlight device means no advertised control. A test-only speaker-volume
provider demonstrates how to add `audio.speaker.volume` with the same API
without ALSA/PulseAudio-specific code in the client.

Node controls also appear as `property.descriptor` and `property.state`
on the persistent server node UUID through the existing object/property
wire protocol. `property.set` is the preferred control path; `/admin
controls` and `/admin control NAME [VALUE]` are administration conveniences.
GTK Admin → Display dynamically enumerates available controls.

Currently the registry supports **integer-valued** controls with bounded
min/max/step. Boolean toggles and enumeration controls should be implemented
as additional typed descriptors; prefer extending the existing
`rr_property_descriptor_t` semantics rather than creating a second widget
protocol. Remote writes are currently gated by owner/admin; the per-control
`privilege` string is metadata only until scoped site privilege enforcement
is connected.

## Candidates for the same mechanism

| Existing subsystem | Candidate controls | Owner and validation |
| --- | --- | --- |
| Raspberry Pi/sBitx | Backlight, display blanking, speaker output volume | Node; backlight driver or mixer |
| `fwdsp` and audio routes | RX monitor gain, mute, input monitoring, routing levels | Media pipeline or audio endpoint, not node-global |
| GPIO mappings | Output level, relay enable, safe restore | Hardware GPIO object with local line allowlist |
| Fan/thermal control | Fan setpoint, policy, sensor readback | Platform object; never bypass thermal safeguards |
| Configured rig controls | RF gain, squelch, tuner, amp state | **Already** represented by rig/VFO property descriptors; do not duplicate in platform registry |
| Serial exports and file shares | Enable/disable or quota adjustment | Their own resource objects; use typed properties and scoped privilege checks |
| SDR and temporary bridges | RX gain/AGC, slice tuning, per-channel volume | Bridge/receiver/VFO resource; do not make ephemeral SDR controls persistent node settings |

The object/property protocol should remain the single discovery and control
surface. Admin pages may render it differently, but must not reimplement
hardware operations on the client.

## Provider checklist

Register one stable property name, a descriptive label and unit, an honest
range and step, read and optional write callbacks, and an access policy.
Avoid publishing controls that are unavailable on this machine. Check current
limits again on write, report unsupported or read-only devices cleanly, audit
writes, and broadcast updated property state to subscribers. Hardware safety
limits are enforced by the backend even for an owner.

Run `rrserver/tests/test_platform_controls.sh` for the hardware-free registry
contract; the real Linux driver needs a Pi/sBitx runtime test.

## Hamlib transceiver backlight (planned)

A Hamlib transceiver display backlight is a **rig property**, not the server
node's `display.brightness` (Linux sysfs backlight). Probe
`rig_has_set_parm(rig, RIG_PARM_BACKLIGHT)` and only advertise a writable
`display.brightness` property on rigs where Hamlib reports support.
Readability must be probed separately with `rig_has_get_parm()`.
Hamlib represents the backlight as a float in [0,1]; present as 0–100%
in the UI and convert at the backend boundary using `rig_set_parm()`.
Never claim a control exists for unsupported rigs, or report a value as
readable when Hamlib cannot read it.

Execute rig I/O in the existing serialized backend worker, not on the GTK
thread. Export through the rig property descriptor/state protocol, which
Resource Explorer already consumes; the Controls tab may filter the same
object-owned descriptors, but must not create a second command namespace.
Apply destination-side scoped write permissions before sending a command.
This integration has not yet been implemented.

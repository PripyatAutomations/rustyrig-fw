# FT-891 operating VFO selection, CAT interoperability and PTT

Status: hardware reconciliation is not implemented. Do not assume the
per-client selected letter names the FT-891 physical transmit register.

## Intended contract

- A client's `!vfo A/B` chooses the target for subsequent operations in
  that same chat command stream. A bare selection does not retroactively
  apply a mode/frequency operation.
- A physical rig has one operating RX/TX path (unless split is deliberately
  enabled). All controlling clients must observe that shared state.
- The FT-891 CAT manual provides `FA;`, `FB;` for stored frequencies,
  `SV;` for swapping the VFO registers, and `ST` for split. These
  operations are **not** equivalent to changing `backend->active_vfo`.
- CAT writes and VFO selection need serialized transactions with readback
  before publishing an active selection. Reject selection while PTT is
  asserted. Lock out PTT until selected physical operating state is known.
- External CAT clients and front-panel actions may change the register
  state: invalidate prior physical-selection assumptions and re-read
  immediately. If a reliable physical-select query is unavailable, track
  only state confirmed by a serialized read/swap transaction; invalidate
  on reconnect and ambiguous external actions.
- Do not issue `SV;` to reconcile merely a client's per-session cursor.
  A shared hardware swap needs explicit rig-operation authority, must
  honor other users and TX interlocks, and cannot occur mid-transmission.
- PTT must target verified physical RX/TX selection. `TX1;` is not a
  VFO-specific CAT operation. Until hardware selection is implemented,
  Yaesu VFO B key-down is explicitly refused by the backend.

## Command parsing verification

Check sequential behavior, in one room:

- `!vfo b mode lsb vfo a` => B LSB; A unchanged; cursor A.
- `!vfo b mode lsb vfo a mode am` => B LSB; A AM; cursor A.
- On FT-891, B mode change currently requires explicit physical operating
  selection because `MD0` applies to the current operating register.
  Report rejection rather than mutating A while claiming B was changed.
- Verify async backend job ordering: SELECT followed by MODE must not
  overtake; failed SELECT must not send a success notification.

## ttyCAT

The unnumbered floating endpoint must resolve to `$HOME/dev/ttyCAT`
by default. `serial:ttyCAT.path` is an explicit override.
Numbered `ttyCAT0`, `ttyCAT1`, etc. are opt-in pinned endpoints.
An old `ttyCAT0` symlink may persist from previous runs, and a local
override can change the floating symlink destination: inspect actual
serial binding config when testing.

Hardware validation is required before enabling VFO B transmission.

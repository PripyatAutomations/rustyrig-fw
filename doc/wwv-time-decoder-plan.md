# Optional WWV/WWVH time decoder: deferred plan

This is a feasible future DSP subprocess, not a small FFTW addition. No decoder,
automatic tuning, time-setting privilege or clock-control implementation is
included in this release. Protocol round-trip latency does not depend on it.

## Keep clocks separate

Measure RTT with a local monotonic clock and a matching acknowledgement. The
sender stores its precise send time, then subtracts that from its local receive
time. Existing request IDs can provide correlation without adding timestamps to
every packet; keepalive timestamps are opaque echoes. Clock synchronization is
not needed. The result includes remote processing time and both network paths;
it cannot reveal either one-way delay separately.

Comparing sender and receiver wall-clock timestamps requires synchronized clocks
and explicit uncertainty. NTP/chrony is the existing practical choice when UTC
alignment matters. A WWV receiver would be an optional clock source, not a
protocol prerequisite. Clock adjustments must never affect monotonic timeouts.

## Signal and implementation work

WWV/WWVH carries a BCD time code on a 100 Hz subcarrier, alongside time markers
and other programme material. It requires pulse classification, minute/frame
alignment, date/leap indicators and confidence checks, not just tone detection.
See [NIST's broadcast format](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwv/wwv-and-wwvh-digital-time-code).

A narrowband filter or Goertzel detector may be sufficient for the subcarrier;
FFTW is optional, and would not supply framing or time interpretation. Start
with deterministic recordings and a streaming parser, then validate with real
reception. Require several consistent frames before declaring clock lock.

Use a dedicated, explicitly selected RX source. Do not retune a station's active
radio, interrupt a QSO or change TX ownership to acquire time. Local capture
should retain sample-clock timing before lossy encoding or network transport.
Buffered/network audio arrival time is not the original RF arrival time.

## Proposed subprocess boundary

A separately spawned worker borrows the existing manager's process lifecycle,
bounded PCM input and diagnostics. Accept sample format/rate and a capture
monotonic origin plus sample position. Return decoded UTC, observation time,
lock state, uncertainty, signal quality and source identity as structured events.
The main program owns configuration and selection of the source; a worker owns
DSP and parsing. Put reusable parsing in a library only if another consumer
needs it. The browser would consume observations from an authorized server
rather than start privileged processes or set an OS clock.

Keep the worker unprivileged. Initially publish observations and offset estimates
only. A later explicit clock-source integration could feed chrony through an
established reference-clock interface; avoid giving the decoder arbitrary
"set time" commands. Include source loss, stale observation and disagreement
with other sources in the contract.

## Timing limits and validation

Account for sound-card/sample-clock error, receiver/filter group delay, HF
propagation, recording offsets, resampling and queueing. NIST notes that
received accuracy depends on propagation and describes typical reception
accuracy rather than nanosecond synchronization; see the
[NIST radio broadcast FAQ](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/radio-station-wwv/nist-radio-broadcasts-frequently).

Test clean synthetic minute frames, noise/fading, interference, missing pulses,
wrong frame alignment, year boundaries, leap indicators, loss/reacquisition,
partial PCM chunks, backlog rejection and hostile output. Compare decoded time
against independently known recordings and a disciplined clock. Do not claim
one-way network latency precision beyond the measured clock uncertainty.

Revisit after the beta's transport, state and media recovery paths are stable.

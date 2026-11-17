# PSK / Olivia / FT8 external modem workers

This is a staging area for isolated modem processes packaged together as `rustyrig-modems`. Neither
third-party library is copied or extracted. Build with:

```sh
make -C modems -j4     # builds PSK, Olivia and FT8
make -C modems test
sudo make -C modems install
```

Installed workers are part of the `rustyrig-modems` package under
`/usr/libexec/rustyrig/modems/`, outside `/usr/bin` and writable state.
The top-level build follows `components.modems` in `config/$PROFILE.config.json`
(`PROFILE=radio` by default). `WITH_MODEMS=yes` and `WITH_MODEMS=no` explicitly
override the profile; `WITH_MODEMS=auto` follows it. FT8 is included whenever modems are enabled.

No CMake is required. GNU Make compiles upstream C99/C++20 sources directly\ninto \`modems/build/\` without changing their source directories.\nRequires a C compiler, a C++20 compiler, GNU Make, ar, and git.\n\nThe build clones upstream `jfrancis42/libpsk` and `jfrancis42/olivia-modem`
to `ext/` only when their source directories do not exist; it never modifies
an existing checkout. For reproducible packages, pin upstream commit SHAs
before release. Offline builds work with existing checkouts.\n\`make -C modems clean\` deletes only RustyRig's modem build output, not \`ext/\`.\n\`make -C modems test\` runs libpsk's five upstream test binaries and\nsmoke-tests both receiver workers with an empty FD 4 stream.

The **client-local PSK and Olivia workers** now implement RX decoding and
experimental text-to-waveform TX. Worker fd 0 accepts `TX <text>\\n` and
`CANCEL\\n`; fd 5 produces 16 kHz mono S16LE samples while fd 1 reports
`TX_DONE` as a packet on the dedicated fd 3 control socket; decoded
text stays on fd 1. RX arrives on fd 4 as 16 kHz mono S16LE. A text send from the
Routing modem menu keys the selected active radio **only when both TX PCM
and PTT control pins are connected to that same radio**, and the operator
has TX control. The core waits for a confirmed rig PTT state before sending
samples to the normal TX encoder, excludes microphone TX, and requests
PTT OFF when finished or on failure/timeout. PTT is sourced specifically
from `modemN.ptt` and can be monitored by the floating ON AIR panel.

**FT8 remains RX-only**; the worker does not have slot-synchronized TX.
The interim 16→8 kHz receive conversion is still a two-sample box average,
and the TX upsampler is also provisional, not a band-limited converter.
The new client TX code has **not been build-tested or RF-tested**; validate
with dummy loads, PTT release/timeout tests and signal measurements before
allowing live transmission. Existing server-owned modem workers remain a
separate implementation with their own fd 3 lease and fd 5 ingress work
still outstanding.

```ini
[serial]
;ttyMODEM0=modem:/usr/libexec/rustyrig/modems/rrmodem-psk BPSK31 1500
;ttyMODEM1=modem:/usr/libexec/rustyrig/modems/rrmodem-olivia 8 500
;[serial:ttyMODEM0]
;rig=rig0
;[serial:ttyMODEM1]
;rig=rig0
```

These exports are examples, commented until installed. **Server-owned**
workers still lack an authorized FD 3 PTT lease and FD 5 TX injection.
The client-local Routing TX implementation above is separate; do not
equate the client PTT/audio gates with a server-owned lease.

Olivia is GPL-3.0-or-later. libpsk source headers claim MIT but its README
license section says GPL-3.0: treat it as GPL-3.0 pending clarification.
Keep both workers separate from rrserver; later move each worker directory
and its build/packaging rules into standalone repositories.

## Weak-signal AWGN benchmarks

```sh
make -C modems test-snr      # PSK31, Olivia 8/500, and FT8
make -C modems ft8-snr       # FT8 alone (also runs inside test-snr)
```

These are *measurement* targets, not pass/fail sensitivity certifications.
They print full-message recovery counts over deterministic Gaussian noise seeds.
PSK31 spans +10 through -45 dB (5 trials/point); Olivia 8/500 spans
+10 through -45 dB (3 trials/point); FT8 spans +5 through -55 dB
(3 trials/point) with a generated 15-second WAV slot. FT8 may take longer.

**SNR definition:** measured clean waveform mean-square divided by injected
noise variance across the **entire sampled Nyquist bandwidth**. This differs
from the conventional FT8 SNR referenced to 2500 Hz. Do not compare the
absolute SNR numbers to WSJT-X reports without reference-bandwidth conversion.
The FT8 SNR is measured over the 6 kHz sampled Nyquist band at 12 kHz;\nthis is 3.80 dB lower than the equivalent 2500 Hz reference SNR.\nOlivia/PSK use 8 kHz input (4 kHz Nyquist bandwidth), which is\n2.04 dB lower than the equivalent 2500 Hz reference SNR.\nA zero-decode result at low SNR is a measurement, not a test failure.\nThe tests are idealized waveform→decoder tests, not over-the-air measurements.
They omit frequency error, fading, interference, clock drift and real audio-path
resampling. The FT8 runner uses the upstream WAV generator and decoder, not the
live rrmodem-ft8 worker's wall-clock and 16-to-12 kHz resampler.

For an exact production sensitivity test, add worker-end-to-end test fixtures
and check both decoding and false positives with noise-only inputs.

## Packaging

Debian, RPM and Arch package all three external workers as `rustyrig-modems`.
Workers install to `/usr/libexec/rustyrig/modems/` rather than `/usr/bin`
or writable runtime state. Normal builds use `WITH_MODEMS=yes`; set
`WITH_MODEMS=no` to build RustyRig without these workers.

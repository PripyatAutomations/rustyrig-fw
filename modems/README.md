# PSK / Olivia / FT8 external modem workers

This is a staging area for isolated modem processes packaged together as `rustyrig-modems`. Neither
third-party library is copied or extracted. Build with:

```sh
make -C modems -j4  # PSK, Olivia and FT8     # builds PSK, Olivia and FT8
make -C modems test
sudo make -C modems install
```

Installed workers are part of the `rustyrig-modems` package under
`/usr/libexec/rustyrig/modems/`, outside `/usr/bin` and writable state.
The top-level build defaults to `WITH_MODEMS=yes`; use `WITH_MODEMS=no`
to skip worker builds. FT8 is included whenever modems are enabled.

No CMake is required. GNU Make compiles upstream C99/C++20 sources directly\ninto \`modems/build/\` without changing their source directories.\nRequires a C compiler, a C++20 compiler, GNU Make, ar, and git.\n\nThe build obtains upstream `jfrancis42/libpsk`, `jfrancis42/olivia-modem` and `kgoba/ft8_lib`
to `ext/` only when their source directories do not exist; it never modifies
an existing checkout. For reproducible packages, pin upstream commit SHAs
before release. Offline builds work with existing checkouts.\n\`make -C modems clean\` deletes only RustyRig's modem build output, not \`ext/\`.\n\`make -C modems test\` runs libpsk's five upstream test binaries and\nsmoke-tests both receiver workers with an empty FD 4 stream.

All three workers currently implement **RX only**. Their fd 4 input uses the
existing rrserver 16 kHz, mono, S16LE stream and fd 1 outputs decoded bytes.
The interim 16→8 kHz conversion is a two-sample box average, **not** a proper
band-limited converter. This can alias high-frequency input: replace with the
shared negotiated resampler before production RF decoding.

```ini
[serial]
;ttyMODEM0=modem:/usr/libexec/rustyrig/modems/rrmodem-psk BPSK31 1500
;ttyMODEM1=modem:/usr/libexec/rustyrig/modems/rrmodem-olivia 8 500
;[serial:ttyMODEM0]
;rig=rig0
;[serial:ttyMODEM1]
;rig=rig0
```

These exports are examples, commented until installed. Existing worker
limitations apply: FD 3 control/PTT leases, PCM negotiation, authorized
FD 5 TX injection, rig-mode routing and modem resources are **not implemented**
by these workers. Do not connect any worker's waveform to RF TX directly.

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

`rustyrig-modems` is the dedicated Debian, RPM, and Arch split package.
The normal build uses `WITH_MODEMS=yes` and includes PSK, Olivia, and FT8;
`WITH_MODEMS=no` disables these workers. Worker executables are packaged at
`/usr/libexec/rustyrig/modems/` (not `/usr/bin` or mutable `/var/lib`).
The client and server execute them as subprocesses; they are not shell tools.

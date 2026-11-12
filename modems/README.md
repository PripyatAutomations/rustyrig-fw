# PSK / Olivia experimental external workers

This is a staging area for independently packaged modem processes. Neither
third-party library is copied or extracted. Build with:

```sh
make -C modems -j4
make -C modems test
sudo make -C modems install
```

No CMake is required. GNU Make compiles upstream C99/C++20 sources directly\ninto \`modems/build/\` without changing their source directories.\nRequires a C compiler, a C++20 compiler, GNU Make, ar, and git.\n\nThe build clones upstream `jfrancis42/libpsk` and `jfrancis42/olivia-modem`
to `ext/` only when their source directories do not exist; it never modifies
an existing checkout. For reproducible packages, pin upstream commit SHAs
before release. Offline builds work with existing checkouts.\n\`make -C modems clean\` deletes only RustyRig's modem build output, not \`ext/\`.\n\`make -C modems test\` runs libpsk's five upstream test binaries and\nsmoke-tests both receiver workers with an empty FD 4 stream.

The workers currently implement **RX only**. Their fd 4 input uses the
existing rrserver 16 kHz, mono, S16LE stream and fd 1 outputs decoded bytes.
The interim 16→8 kHz conversion is a two-sample box average, **not** a proper
band-limited converter. This can alias high-frequency input: replace with the
shared negotiated resampler before production RF decoding.

```ini
[serial]
;ttyMODEM0=modem:/var/lib/rustyrig/modems/rrmodem-psk BPSK31 1500
;ttyMODEM1=modem:/var/lib/rustyrig/modems/rrmodem-olivia 8 500
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

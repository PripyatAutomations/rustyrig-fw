# RustyRig FT8 receive-only modem worker

An external executable using [kgoba/ft8_lib](https://github.com/kgoba/ft8_lib)
(MIT licensed). It is deliberately **not** linked into rrserver.

## Build (optional)

```sh
git clone https://github.com/kgoba/ft8_lib ext/ft8_lib
make -C modems/ft8 FT8_LIB=../../ext/ft8_lib
```

The normal RustyRig build does not build or require ft8_lib.\nOptional Debian build: WITH_FT8=yes dpkg-buildpackage -b -us -uc, with\nft8_lib checked out under ext/ft8_lib. It creates rustyrig-modem-ft8.\n The optional
worker can be copied to a known absolute path or launched from your build
tree. Do not launch through an untrusted shell.

## Worker interface

- Child fd 0: line-oriented commands (`RAW\n`, `JSON\n`, `HELP\n`).
- Child fd 1: **one decoded message per line**, JSON by default.
- Child fd 2: diagnostics only.
- Child fd 4: mono signed 16-bit little-endian RX audio at **16 kHz**.
- Child fd 3/5: reserved; no rig control or TX support in this executable.

`RAW` switches output to plain decoded text, one decode per line. `JSON`
returns to the default JSON-lines mode. These commands are case-sensitive
and take effect immediately for subsequent decodes.

Example JSON record (illustrative):

```json
{"type":"ft8.decode","slot":1792245600,"audio_hz":1200.0,"dt":0.25,"sync_score":18,"text":"CQ K1ABC FN42"}
```

`slot` is the Unix time of the UTC 15-second slot, `audio_hz` is the
detected offset in the received audio passband, `dt` is timing offset in
seconds, and `sync_score` is ft8_lib's synchronization candidate score,
**not calibrated signal-to-noise ratio**. An RF frequency can be derived
later from the selected VFO/SDR slice's metadata; the worker currently
does not guess it. Output is binary-safe UTF-8 JSON escaping. Unknown
hashed callsigns may remain unresolved because a persistent callsign-hash
dictionary has not been implemented.

## Server config example (disabled by default)

```ini
[serial]
;ttyMODEM0=modem:/var/lib/rustyrig/modems/rrmodem-ft8

[serial:ttyMODEM0]
rig=rig0
;log=/var/log/rustyrig/ttyMODEM0.log
```

A configured persistent modem receives RX PCM even without a connected
terminal. The worker counts as receiver demand for rrbridge, so it can
activate a bridge-backed rig when no one is listening. The bridge still
starts only for requested receivers, not mere discovery.

**Limitations:** This is the first experimental RX decoder. Live wall-clock
slot timing assumes low-latency, continuous PCM; late bridge/resampler
audio can distort slot alignment. The initial 16-to-12 kHz linear
interpolation is intentionally simple and needs a band-limited resampler.
The current server RX fanout uses best-effort nonblocking writes without
framed discontinuity markers; overload may corrupt a 15-second slot.
No FT8 transmit capability exists. Test with known IQ/WAV fixtures and a
live bridge before expecting reliable over-the-air decodes.

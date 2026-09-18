# Audio recording filenames

Recordings use local wall-clock time at recording start:

```
YYYYMMDD.HHMMSS.username.direction.flac
20260916.145901.admin.tx.flac
20260916.145901.radio.rx.flac
```

Direction is radio RX/TX, not fwdsp encoder/decoder mode. Shared RX recordings
use `radio`; TX uses the transmitting username. The native client uses its
configured login name for its own TX recording. Server automatic TX recording
starts at PTT and stops at key-up; RX recording starts with the RX pipeline.
The existing `record.rx`, `record.tx`, and `path.record-dir` settings apply.

Characters outside ASCII letters, digits, `_`, and `-` in the username become
`_`. Names are reserved exclusively, so restarting within one second or
recording multiple channels never overwrites a file. Collisions add a suffix:
`20260916.145901.admin.tx.1.flac`, `.2.flac`, etc.

Codec, VFO, frequency, and other session metadata belong in the database;
they are not encoded in this filename. Database association is separate from
filename generation (the PTT log currently leaves `record_file` empty).

The manager passes recording identity through local control IPC. Rebuild and
restart fwdsp, libfwdspmgr, rrclient, and rrserver together after changing that
structure. This does not change WebSocket media messages.

The headless regression test requires built binaries, GStreamer's
`audiotestsrc`/`appsink` plugins, Python 3, and the `flac`/`metaflac` tools:

```
bash tests/run-tests.sh fwdsp
```

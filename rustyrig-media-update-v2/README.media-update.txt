rustyrig-fw media codec switching + fwdsp recording update v2

This archive is laid out to extract at the repository root.

Changes:
- Codec selection is per media channel UUID.
- Client no longer pre-starts pc16 before negotiation.
- Codec changes replace the active decoder immediately.
- Unused decoders stop immediately.
- Unused encoders are PAUSED and retained for fwdsp.hangtime, then reaped.
  Selecting the same encoder again during hangtime RESUMEs it.
- recording-start / recording-stop server event placeholders control the
  channel's active fwdsp instance.
- record.rx / record.tx enable recording automatically.
- Raw S16LE recording is copied into a non-blocking ring and FLAC-compressed
  by a pthread worker.
- record.buffer-size now controls the ring size (minimum 4096 bytes).
- path.record-dir controls output location.
- mk/compile.mk adds libFLAC and pthread flags to fwdsp only.
- rrclient.cfg and rrserver.cfg now include record-sink tee branches for pc16
  and opus.

Build dependency on Debian/Devuan:
  apt install libflac-dev

Important config cleanup:
- codecs.allowed is set to "opus pc16" because these are the two network audio
  codecs for which the supplied configs actually define pipelines.
- The old pc16 pipeline was actually mu-law. pc16 is now genuine S16LE PCM.
- mu16/mu08/flac can be re-added to codecs.allowed after matching network
  pipelines are defined. FLAC recording does NOT require FLAC to be a wire codec.

The current tx pipelines still use audiotestsrc because that is what the supplied
configs use. Replace audiotestsrc with the real capture source when ready; keep
its raw S16LE conversion immediately before tee name=t so recording remains on
sound-card-side PCM.

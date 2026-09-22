# Audio processor groundwork

`fwdsp -T` selects a generic framed PCM pipeline, separate from codec encoder
and decoder modes. `-P <name>` selects the configured pipeline
(`pipeline:proc.<name>`); with an inline `-p` pipeline, `-P` is optional. Named
source, sink, and recoder pipelines use `pipeline:src.<name>`,
`pipeline:sink.<name>`, and `pipeline:recode.<name>`, respectively.
The pipeline may be bidirectional for effects, source-only to capture host audio,
or sink-only to play framed PCM to a host audio device. The optional `-M`
argument lets the manager require the matching endpoint shape and fail quickly
if a configured pipeline does not provide it.

The manager exposes `fwdsp_processor_start()` for bidirectional `proc.*` effects,
`fwdsp_audio_capture_start()` for a `src.*` endpoint, and
`fwdsp_audio_playback_start()` for a `sink.*` endpoint. Output is delivered through a
callback on the manager/event-loop thread, so callbacks must return promptly.
The server's `src.rig0` and `sink.rig0` pipelines use PulseAudio (including
PipeWire's PulseAudio compatibility layer) by default and can be overridden in
the normal `[pipelines]` config section. Set `device=` in the pipelines when
the rig audio interface is not the host default. For testing, `src.pink` and
`src.tone660` generate PCM sources; select one by setting
`[fwdsp] rig0.rx-source=src.pink` or `src.tone660`, then restart rrserver.
The selected rig RX source is framed into each active VFO RX encoder. Incoming
talker packets are sent to other subscribers on that VFO, excluding the origin,
decoded to canonical PCM, and written to `sink.rig0`. The decoder's optional
`hub-sink` branch is enabled for server codec processes by `[fwdsp] pcm-hub=true`;
it emits a distinct framed PCM tap to the manager. `proc.*` remains reserved
for effects/transforms, while `recode.*` is reserved for codec recoders.

Every IPC side carries mono S16LE PCM at 16 kHz in the existing `fwdsp` frame
format: a four-byte big-endian byte count, followed by sample bytes. Input and
output GStreamer endpoints use `appsrc name=processor-src` and
`appsink name=processor-sink`, respectively. The fwdsp child validates the
caps and rejects non-PCM output.

This is the first server-side PCM hub path. The audio endpoints can now connect
the host sound interface to the hub; selectable test sources remain available.
Per-rig/per-VFO effect chains and hardware backend integration remain follow-up
work. The shared media channel codec negotiation remains in force, so each VFO
currently has one active encoder format for its listeners.

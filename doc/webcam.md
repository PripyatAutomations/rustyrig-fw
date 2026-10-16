# Webcam capture and viewing

Enable the station webcam on the server:

```ini
[general]
webcam.enable=true
webcam.codec=h264
```

Restart the server, connect a GTK client and run `/webcam`. The server advertises
`station.webcam.rx`; the viewer subscribes automatically when it is the only
video channel. With multiple video channels, use `/media SUB <uuid|#>`.
The viewer offers Codec and Frames/sec selectors and an Apply button. Choices
come from the server's configured delivery profiles. Source codec/rate selects
the original capture stream. Closing the viewer unsubscribes it.

Capture starts when the first subscriber arrives and stops after the last one
leaves. Matching profile requests share a process; different parameters use
separate processes. Idle profile processes stop automatically. Each subscriber
keeps its own selection, so changing one viewer does not change other viewers.
The existing fwdsp subprocess limit also bounds video work.

## Pipelines own codec settings

`webcam.codec` selects the four-character wire tag and its capture pipeline,
`[pipelines] <codec>.tx`. The pipeline owns the encoder, device, dimensions,
frame rate, bitrate and other codec settings. `webcam.device` is a legacy hint;
set `device=` in the capture pipeline. No viewer code selects GStreamer plugins
or overwrites configured video buffering/rate properties.

The default capture pipeline uses H.264, 640x480 at 24 fps, with a low latency
software encoder. Override it for the hardware and workload of the station:

```ini
[pipelines]
h264.tx=v4l2src device=/dev/video0 ! videoconvert ! video/x-raw,format=I420,width=640,height=480,framerate=24/1 ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=256 key-int-max=24 byte-stream=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=tx-sink sync=false max-buffers=1 drop=true
```

Capture/transcode pipelines output complete encoded frames/access units through
`appsink name=tx-sink`. Use Annex B byte-stream access units for H.264/H.265 and
repeat parameter sets at keyframes, so late subscribers can begin decoding.
Frames must fit the existing media payload limit of 65,507 bytes. Reduce the
configured bitrate/resolution/keyframe size if necessary.

Delivery profiles are ordinary pipeline entries named
`video.<four-character-codec>.<integer-fps>`. Nonempty entries are advertised to
viewers; empty entries disable a choice. Defaults provide H.264 and JPEG at
1, 5, 10, 15 and 24 fps, transcoding the default H.264 capture stream. Profile
pipelines must match the configured capture input; adjust them if its format
changes. For example:

```ini
[pipelines]
video.h264.5=appsrc name=rx-src is-live=true format=time do-timestamp=true caps="video/x-h264,stream-format=byte-stream,alignment=au" ! h264parse ! avdec_h264 ! videoconvert ! videorate ! video/x-raw,framerate=5/1 ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=128 key-int-max=5 byte-stream=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=tx-sink sync=false max-buffers=1 drop=true
```

H.265 delivery can be offered with `video.h265.<fps>` entries using the desired
encoder pipeline. H.265 encoding is not enabled by default. Plugin availability
and hardware acceleration depend on the host installation.

## Native and browser display

Native GTK runs the corresponding `[pipelines] <codec>.rx` decoder in `fwdsp`.
Defaults decode H.264, H.265 and JPEG to local PNM images through
`appsink name=rx-sink`; any image format supported by GdkPixbuf can be used at
this local display boundary. Encoded video crosses the network. GTK retains
only the latest pending display image and creates/updates widgets on its main
thread. TUI preserves discovery/subscription commands but has no video display.

The browser also offers `/webcam`. It displays configured JPEG profiles and
releases image object URLs on replacement/close. Native H.264/H.265 decoding is
pipeline based; the browser's JPEG display is a frontend-specific fallback.

## Wire behavior

`media.available.profiles` (internal `media.profiles`) is a space-separated list
of configured profile names. A subscribed viewer selects one with
`media.profile`, its `chan-uuid`, and `profile`; an empty profile selects the
source stream. The server acknowledges the selection with the same operation
and its effective codec. The channel UUID/stream remains stable per connection,
and every binary video frame carries its actual codec. Unsubscribed or
unauthenticated clients cannot select a profile. Profile names never supply
arbitrary pipeline text from clients.

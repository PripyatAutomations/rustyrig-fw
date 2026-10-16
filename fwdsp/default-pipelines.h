//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native audio defaults. Keep both supplied configuration files in sync.
#ifndef FWDSP_DEFAULT_PIPELINES_H
#define FWDSP_DEFAULT_PIPELINES_H

#define FWDSP_DEFAULT_CODECS \
        "opus pc16 g722 mu16 mu08 oggv aacv flac pc1T g72T mu1T mu0T opuT oggT aacT flaT pc1P g72P mu1P mu0P opuP oggP aacP flaP"

/* pulsesrc is the desktop default-source API and is provided by PipeWire's PulseAudio compatibility server on PipeWire systems. Deployments that do not provide
 * that compatibility layer can override the TX pipeline with a pipewiresrc-based definition in their config. */
#define FWDSP_CAPTURE_SOURCE \
        "pulsesrc name=tx-source client-name=fwdsp-tx do-timestamp=true ! audioconvert ! audioresample"
#define FWDSP_NOISE_SOURCE \
        "audiotestsrc is-live=true wave=pink-noise volume=0.15 samplesperbuffer=320 do-timestamp=true"

#define FWDSP_RIG_PCM_SOURCE \
        "appsrc name=tx-src is-live=true format=time do-timestamp=true " \
        "caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"

#define FWDSP_PCM_HOST_MIC \
        "pulsesrc name=processor-mic client-name=fwdsp-mic do-timestamp=true ! " \
        "audioconvert ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "appsink name=processor-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_PCM_HOST_SPEAKER \
        "appsrc name=processor-src is-live=true format=time do-timestamp=true " \
        "caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "audioconvert ! audioresample ! " \
        "pulsesink device=default name=processor-speaker client-name=fwdsp-speaker sync=false"

#define FWDSP_PCM_CLIENT_SPEAKER \
        "appsrc name=processor-src is-live=true format=time do-timestamp=true " \
        "caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "volume name=processor-vol ! audioconvert ! audioresample ! " \
        "pulsesink device=default name=processor-speaker client-name=rrclient-speaker sync=false"

// Dedicated stereo mix input; the original mono endpoint remains usable by DCC.
#define FWDSP_PCM_CLIENT_MIX \
        "appsrc name=processor-src is-live=true format=time do-timestamp=true " \
        "caps=audio/x-raw,format=S16LE,rate=16000,channels=2,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "volume name=processor-vol ! audioconvert ! audioresample ! " \
        "pulsesink device=default name=processor-speaker client-name=rrclient-speaker sync=false"

// Temporary PCM hub endpoints used by rrserver while rig audio hardware is
// not yet connected. Keep these separate from codec TX/RX pipelines so the
// hub has stable canonical-PCM source and sink points.
#define FWDSP_PCM_RIG_RX_TEST_SOURCE \
        "audiotestsrc is-live=true wave=pink-noise volume=0.15 " \
        "samplesperbuffer=320 do-timestamp=true ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "appsink name=processor-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_PCM_RIG_TX_FILE_SINK \
        "appsrc name=processor-src is-live=true format=time do-timestamp=true " \
        "caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! " \
        "audioconvert ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "vorbisenc quality=0.3 ! oggmux ! filesink location=./rig-tx.ogg"

#define FWDSP_PC16_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        " tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_PC16_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_G722_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/G722,rate=16000,channels=1 ! " \
        " avdec_g722 ! " \
        "audioconvert ! " \
        "audioresample ! " \
        " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_G722_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "avenc_g722 bitrate=64000 ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_MU16_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-mulaw,rate=16000,channels=1 ! " \
        " mulawdec ! " \
        "audioconvert ! " \
        "audioresample ! " \
        " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_MU16_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "mulawenc ! " \
        "audio/x-mulaw,rate=16000,channels=1 ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_MU08_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-mulaw,rate=8000,channels=1 ! " \
        " mulawdec ! " \
        "audioconvert ! " \
        "audioresample ! " \
        " audio/x-raw,format=S16LE,rate=8000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_MU08_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audioresample ! " \
        "audio/x-raw,format=S16LE,rate=8000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "mulawenc ! " \
        "audio/x-mulaw,rate=8000,channels=1 ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_OPUS_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-opus,rate=16000,channels=1,channel-mapping-family=0 ! " \
        " opusdec ! " \
        "audioconvert ! " \
        "audioresample ! " \
        " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_OPUS_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "opusenc name=rr-encoder audio-type=generic frame-size=20 bitrate=24000 bitrate-type=vbr ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_AAC_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/mpeg,mpegversion=4,stream-format=adts,framed=true,rate=16000,channels=1 ! " \
        " aacparse ! avdec_aac ! audioconvert ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "tee name=t  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_AAC_TX(source) \
        source " ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved ! " \
        "volume name=tx-vol ! tee name=t  t. ! " \
        "queue max-size-buffers=2 leaky=downstream ! " \
        "avenc_aac bitrate=32000 ! aacparse ! audio/mpeg,mpegversion=4,stream-format=adts,framed=true ! " \
        "appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
        "queue max-size-buffers=4 leaky=downstream ! audioconvert ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_OGGV_RX \
        "appsrc name=rx-src is-live=true format=bytes caps=application/ogg ! " \
        " tee name=encoded-t " \
        " encoded-t. ! queue max-size-buffers=8 leaky=downstream ! " \
        " appsink name=record-encoded-sink emit-signals=false sync=false max-buffers=8 drop=true " \
        " encoded-t. ! queue ! oggdemux ! " \
        " vorbisdec ! " \
        " audioconvert ! " \
        " audioresample ! " \
        " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        " tee name=t t. ! " \
        " queue max-size-buffers=4 leaky=downstream ! " \
        " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_OGGV_TX(source) \
        source " ! " \
        "volume name=tx-vol ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        " tee name=t t. ! " \
        " queue max-size-buffers=2 leaky=downstream ! " \
        " audioconvert ! " \
        " vorbisenc name=rr-encoder quality=0.3 ! " \
        " oggmux max-delay=20000000 max-page-delay=20000000 ! " \
        " tee name=encoded-t " \
        " encoded-t. ! queue max-size-buffers=8 ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false " \
        " encoded-t. ! queue max-size-buffers=8 leaky=downstream ! " \
        " appsink name=record-encoded-sink emit-signals=false sync=false max-buffers=8 drop=true t. ! " \
        " queue max-size-buffers=4 leaky=downstream ! " \
        " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_FLAC_RX \
        "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-flac,framed=true ! " \
        " tee name=encoded-t encoded-t. ! queue max-size-buffers=8 leaky=downstream ! " \
        " appsink name=record-encoded-sink emit-signals=false sync=false max-buffers=8 drop=true " \
        " encoded-t. ! queue ! flacparse ! flacdec ! audioconvert ! audioresample ! " \
        " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! tee name=t " \
        " t. ! " \
        " queue max-size-buffers=4 leaky=downstream ! " \
        " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true t. ! queue max-size-buffers=8 leaky=downstream ! audioresample ! " \
        "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! appsink name=hub-sink emit-signals=false sync=false max-buffers=5 drop=true"

#define FWDSP_FLAC_TX(source) \
        source " ! volume name=tx-vol ! audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
        " tee name=t t. ! queue max-size-buffers=2 leaky=downstream ! flacenc ! tee name=encoded-t " \
        " encoded-t. ! queue max-size-buffers=8 ! " \
        " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false " \
        " encoded-t. ! queue max-size-buffers=8 leaky=downstream ! " \
        " appsink name=record-encoded-sink emit-signals=false sync=false max-buffers=8 drop=true t. ! " \
        " queue max-size-buffers=4 leaky=downstream ! " \
        " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

/* Video pipelines exchange complete access units on the network and PNM
 * frames locally for display. All codec/rate/device choices live here/config. */
#define FWDSP_VIDEO_DECODE "appsrc name=rx-src is-live=true format=time do-timestamp=true "
#define FWDSP_H264_INPUT FWDSP_VIDEO_DECODE "caps=\"video/x-h264,stream-format=byte-stream,alignment=au\" ! h264parse ! avdec_h264 ! videoconvert ! "
#define FWDSP_H264_ENCODE "x264enc tune=zerolatency speed-preset=ultrafast bitrate=256 key-int-max=24 byte-stream=true ! h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! "
#define FWDSP_VIDEO_SINK "appsink name=tx-sink sync=false max-buffers=1 drop=true"
#define FWDSP_VIDEO_PROFILE(codec, fps, encoder) \
   { "pipeline:video." codec "." #fps, FWDSP_H264_INPUT "videorate ! video/x-raw,framerate=" #fps "/1 ! " encoder FWDSP_VIDEO_SINK, "Camera delivery profile" },
#define FWDSP_VIDEO_PIPELINE_DEFAULTS \
   { "pipeline:h264.tx", "v4l2src device=/dev/video0 ! videoconvert ! video/x-raw,width=640,height=480,framerate=24/1 ! " FWDSP_H264_ENCODE FWDSP_VIDEO_SINK, "Default H.264 camera capture" }, \
   { "pipeline:h264.rx", FWDSP_H264_INPUT "video/x-raw,format=RGB ! pnmenc ! appsink name=rx-sink sync=false max-buffers=1 drop=true", "H.264 to local display frames" }, \
   { "pipeline:h265.rx", FWDSP_VIDEO_DECODE "caps=\"video/x-h265,stream-format=byte-stream,alignment=au\" ! h265parse ! avdec_h265 ! videoconvert ! video/x-raw,format=RGB ! pnmenc ! appsink name=rx-sink sync=false max-buffers=1 drop=true", "H.265 to local display frames" }, \
   { "pipeline:jpeg.tx", "v4l2src device=/dev/video0 ! videoconvert ! video/x-raw,width=640,height=480,framerate=15/1 ! jpegenc quality=70 ! " FWDSP_VIDEO_SINK, "Default JPEG camera capture" }, \
   { "pipeline:jpeg.rx", FWDSP_VIDEO_DECODE "caps=image/jpeg ! jpegparse ! jpegdec ! videoconvert ! video/x-raw,format=RGB ! pnmenc ! appsink name=rx-sink sync=false max-buffers=1 drop=true", "JPEG to local display frames" }, \
   FWDSP_VIDEO_PROFILE("h264", 1, FWDSP_H264_ENCODE) \
   FWDSP_VIDEO_PROFILE("h264", 5, FWDSP_H264_ENCODE) \
   FWDSP_VIDEO_PROFILE("h264", 10, FWDSP_H264_ENCODE) \
   FWDSP_VIDEO_PROFILE("h264", 15, FWDSP_H264_ENCODE) \
   FWDSP_VIDEO_PROFILE("h264", 24, FWDSP_H264_ENCODE) \
   FWDSP_VIDEO_PROFILE("jpeg", 1, "jpegenc quality=70 ! ") \
   FWDSP_VIDEO_PROFILE("jpeg", 5, "jpegenc quality=70 ! ") \
   FWDSP_VIDEO_PROFILE("jpeg", 10, "jpegenc quality=70 ! ") \
   FWDSP_VIDEO_PROFILE("jpeg", 15, "jpegenc quality=70 ! ") \
   FWDSP_VIDEO_PROFILE("jpeg", 24, "jpegenc quality=70 ! ")

#define FWDSP_AUDIO_PIPELINE_DEFAULTS(source) \
        { "pipeline:pc16.rx", FWDSP_PC16_RX, "Default pc16.rx audio pipeline" }, \
        { "pipeline:pc16.tx", FWDSP_PC16_TX(source), "Default pc16.tx audio pipeline" }, \
        { "pipeline:pc1T.rx", FWDSP_PC16_RX, "Default pc1T.rx audio pipeline" }, \
        { "pipeline:pc1T.tx", \
          FWDSP_PC16_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default pc1T.tx audio pipeline" }, \
        { "pipeline:pc1P.rx", FWDSP_PC16_RX, "Default pc1P.rx audio pipeline" }, \
        { "pipeline:pc1P.tx", FWDSP_PC16_TX(FWDSP_NOISE_SOURCE), "Default pc1P.tx audio pipeline" }, \
        { "pipeline:g722.rx", FWDSP_G722_RX, "Default g722.rx audio pipeline" }, \
        { "pipeline:g722.tx", FWDSP_G722_TX(source), "Default g722.tx audio pipeline" }, \
        { "pipeline:g72T.rx", FWDSP_G722_RX, "Default g72T.rx audio pipeline" }, \
        { "pipeline:g72T.tx", \
          FWDSP_G722_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=160 do-timestamp=true"), \
          "Default g72T.tx audio pipeline" }, \
        { "pipeline:g72P.rx", FWDSP_G722_RX, "Default g72P.rx audio pipeline" }, \
        { "pipeline:g72P.tx", FWDSP_G722_TX(FWDSP_NOISE_SOURCE), "Default g72P.tx audio pipeline" }, \
        { "pipeline:mu16.rx", FWDSP_MU16_RX, "Default mu16.rx audio pipeline" }, \
        { "pipeline:mu16.tx", FWDSP_MU16_TX(source), "Default mu16.tx audio pipeline" }, \
        { "pipeline:mu1T.rx", FWDSP_MU16_RX, "Default mu1T.rx audio pipeline" }, \
        { "pipeline:mu1T.tx", \
          FWDSP_MU16_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default mu1T.tx audio pipeline" }, \
        { "pipeline:mu1P.rx", FWDSP_MU16_RX, "Default mu1P.rx audio pipeline" }, \
        { "pipeline:mu1P.tx", FWDSP_MU16_TX(FWDSP_NOISE_SOURCE), "Default mu1P.tx audio pipeline" }, \
        { "pipeline:mu08.rx", FWDSP_MU08_RX, "Default mu08.rx audio pipeline" }, \
        { "pipeline:mu08.tx", FWDSP_MU08_TX(source), "Default mu08.tx audio pipeline" }, \
        { "pipeline:mu0T.rx", FWDSP_MU08_RX, "Default mu0T.rx audio pipeline" }, \
        { "pipeline:mu0T.tx", \
          FWDSP_MU08_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default mu0T.tx audio pipeline" }, \
        { "pipeline:mu0P.rx", FWDSP_MU08_RX, "Default mu0P.rx audio pipeline" }, \
        { "pipeline:mu0P.tx", FWDSP_MU08_TX(FWDSP_NOISE_SOURCE), "Default mu0P.tx audio pipeline" }, \
        { "pipeline:opus.rx", FWDSP_OPUS_RX, "Default opus.rx audio pipeline" }, \
        { "pipeline:opus.tx", FWDSP_OPUS_TX(source), "Default opus.tx audio pipeline" }, \
        { "pipeline:opuT.rx", FWDSP_OPUS_RX, "Default opuT.rx audio pipeline" }, \
        { "pipeline:opuT.tx", \
          FWDSP_OPUS_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default opuT.tx audio pipeline" }, \
        { "pipeline:opuP.rx", FWDSP_OPUS_RX, "Default opuP.rx audio pipeline" }, \
        { "pipeline:opuP.tx", FWDSP_OPUS_TX(FWDSP_NOISE_SOURCE), "Default opuP.tx audio pipeline" }, \
        { "pipeline:aacv.rx", FWDSP_AAC_RX, "Default aacv.rx audio pipeline" }, \
        { "pipeline:aacv.tx", FWDSP_AAC_TX(source), "Default aacv.tx audio pipeline" }, \
        { "pipeline:aacT.rx", FWDSP_AAC_RX, "Default aacT.rx audio pipeline" }, \
        { "pipeline:aacT.tx", \
          FWDSP_AAC_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default aacT.tx audio pipeline" }, \
        { "pipeline:aacP.rx", FWDSP_AAC_RX, "Default aacP.rx audio pipeline" }, \
        { "pipeline:aacP.tx", FWDSP_AAC_TX(FWDSP_NOISE_SOURCE), "Default aacP.tx audio pipeline" }, \
        { "pipeline:oggv.rx", FWDSP_OGGV_RX, "Default oggv.rx audio pipeline" }, \
        { "pipeline:oggv.tx", FWDSP_OGGV_TX(source), "Default oggv.tx audio pipeline" }, \
        { "pipeline:oggT.rx", FWDSP_OGGV_RX, "Default oggT.rx audio pipeline" }, \
        { "pipeline:oggT.tx", \
          FWDSP_OGGV_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default oggT.tx audio pipeline" }, \
        { "pipeline:oggP.rx", FWDSP_OGGV_RX, "Default oggP.rx audio pipeline" }, \
        { "pipeline:oggP.tx", FWDSP_OGGV_TX(FWDSP_NOISE_SOURCE), "Default oggP.tx audio pipeline" }, \
        { "pipeline:flac.rx", FWDSP_FLAC_RX, "Default flac.rx audio pipeline" }, \
        { "pipeline:flac.tx", FWDSP_FLAC_TX(source), "Default flac.tx audio pipeline" }, \
        { "pipeline:flaT.rx", FWDSP_FLAC_RX, "Default flaT.rx audio pipeline" }, \
        { "pipeline:flaT.tx", \
          FWDSP_FLAC_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), \
          "Default flaT.tx audio pipeline" }, \
        { "pipeline:flaP.rx", FWDSP_FLAC_RX, "Default flaP.rx audio pipeline" }, \
        { "pipeline:flaP.tx", FWDSP_FLAC_TX(FWDSP_NOISE_SOURCE), "Default flaP.tx audio pipeline" }, \
        { "pipeline:src.host-mic", FWDSP_PCM_HOST_MIC, "Host microphone to canonical PCM" }, \
        { "pipeline:sink.host-speaker", FWDSP_PCM_HOST_SPEAKER, "Canonical PCM to host speaker" }, \
        { "pipeline:src.client.dsp0", FWDSP_PCM_HOST_MIC, "Client microphone to canonical PCM" }, \
        { "pipeline:sink.client.mix", FWDSP_PCM_CLIENT_MIX, "Stereo client RX mixer to speaker" }, \
        { "pipeline:sink.client.dsp0", FWDSP_PCM_CLIENT_SPEAKER, "Canonical PCM to client speaker" }, \
        { "pipeline:src.rig0", FWDSP_PCM_RIG_RX_TEST_SOURCE, "Temporary pink-noise rig RX source to canonical PCM" }, \
        { "pipeline:sink.rig0", FWDSP_PCM_RIG_TX_FILE_SINK, "Temporary canonical PCM rig TX sink to Ogg/Vorbis file" },

#endif

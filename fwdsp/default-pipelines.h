// Native audio defaults. Keep both supplied configuration files in sync.
#ifndef FWDSP_DEFAULT_PIPELINES_H
#define FWDSP_DEFAULT_PIPELINES_H

#define FWDSP_DEFAULT_CODECS "pc16 g722 mu16 mu08 opus oggv pc1T g72T mu1T mu0T opuT oggT"

#define FWDSP_CAPTURE_SOURCE "pulsesrc name=tx-source client-name=fwdsp-tx do-timestamp=true ! audioconvert ! audioresample"
#define FWDSP_NOISE_SOURCE "audiotestsrc is-live=true wave=pink-noise volume=0.15 samplesperbuffer=320 do-timestamp=true"

#define FWDSP_PC16_RX \
   "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
   " tee name=t  t. ! " \
   "queue max-size-buffers=2 leaky=downstream ! " \
   "volume name=rx-vol ! " \
   "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

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
   "queue max-size-buffers=2 leaky=downstream ! " \
   "volume name=rx-vol ! " \
   "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

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
   "queue max-size-buffers=2 leaky=downstream ! " \
   "volume name=rx-vol ! " \
   "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

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
   "queue max-size-buffers=2 leaky=downstream ! " \
   "volume name=rx-vol ! " \
   "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_MU08_TX(source) \
   source " ! " \
   "volume name=tx-vol ! " \
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
   "queue max-size-buffers=2 leaky=downstream ! " \
   "volume name=rx-vol ! " \
   "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_OPUS_TX(source) \
   source " ! " \
   "volume name=tx-vol ! " \
   "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
   "tee name=t  t. ! " \
   "queue max-size-buffers=2 leaky=downstream ! " \
   "opusenc audio-type=generic frame-size=20 bitrate=24000 bitrate-type=cbr ! " \
   " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
   "queue max-size-buffers=4 leaky=downstream ! " \
   "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_OGGV_RX \
   "appsrc name=rx-src is-live=true format=bytes caps=application/ogg ! " \
   " oggdemux ! " \
   " vorbisdec ! " \
   " audioconvert ! " \
   " audioresample ! " \
   " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
   " tee name=t t. ! " \
   " queue max-size-buffers=2 leaky=downstream ! " \
   " volume name=rx-vol ! " \
   " pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false t. ! " \
   " queue max-size-buffers=4 leaky=downstream ! " \
   " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_OGGV_TX(source) \
   source " ! " \
   "volume name=tx-vol ! " \
   "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
   " tee name=t t. ! " \
   " queue max-size-buffers=2 leaky=downstream ! " \
   " audioconvert ! " \
   " vorbisenc quality=0.3 ! " \
   " oggmux max-delay=20000000 max-page-delay=20000000 ! " \
   " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false t. ! " \
   " queue max-size-buffers=4 leaky=downstream ! " \
   " appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true"

#define FWDSP_AUDIO_PIPELINE_DEFAULTS(source) \
   { "pipeline:pc16.rx", FWDSP_PC16_RX, "Default pc16.rx audio pipeline" }, \
   { "pipeline:pc16.tx", FWDSP_PC16_TX(source), "Default pc16.tx audio pipeline" }, \
   { "pipeline:pc1T.rx", FWDSP_PC16_RX, "Default pc1T.rx audio pipeline" }, \
   { "pipeline:pc1T.tx", FWDSP_PC16_TX("audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true"), "Default pc1T.tx audio pipeline" }, \
   { "pipeline:g722.rx", FWDSP_G722_RX, "Default g722.rx audio pipeline" }, \
   { "pipeline:g722.tx", FWDSP_G722_TX(source), "Default g722.tx audio pipeline" }, \
   { "pipeline:g72T.rx", FWDSP_G722_RX, "Default g72T.rx audio pipeline" }, \
   { "pipeline:g72T.tx", FWDSP_G722_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=160 do-timestamp=true"), "Default g72T.tx audio pipeline" }, \
   { "pipeline:mu16.rx", FWDSP_MU16_RX, "Default mu16.rx audio pipeline" }, \
   { "pipeline:mu16.tx", FWDSP_MU16_TX(source), "Default mu16.tx audio pipeline" }, \
   { "pipeline:mu1T.rx", FWDSP_MU16_RX, "Default mu1T.rx audio pipeline" }, \
   { "pipeline:mu1T.tx", FWDSP_MU16_TX("audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true"), "Default mu1T.tx audio pipeline" }, \
   { "pipeline:mu08.rx", FWDSP_MU08_RX, "Default mu08.rx audio pipeline" }, \
   { "pipeline:mu08.tx", FWDSP_MU08_TX(source), "Default mu08.tx audio pipeline" }, \
   { "pipeline:mu0T.rx", FWDSP_MU08_RX, "Default mu0T.rx audio pipeline" }, \
   { "pipeline:mu0T.tx", FWDSP_MU08_TX("audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true"), "Default mu0T.tx audio pipeline" }, \
   { "pipeline:opus.rx", FWDSP_OPUS_RX, "Default opus.rx audio pipeline" }, \
   { "pipeline:opus.tx", FWDSP_OPUS_TX(source), "Default opus.tx audio pipeline" }, \
   { "pipeline:opuT.rx", FWDSP_OPUS_RX, "Default opuT.rx audio pipeline" }, \
   { "pipeline:opuT.tx", FWDSP_OPUS_TX("audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true"), "Default opuT.tx audio pipeline" }, \
   { "pipeline:oggv.rx", FWDSP_OGGV_RX, "Default oggv.rx audio pipeline" }, \
   { "pipeline:oggv.tx", FWDSP_OGGV_TX(source), "Default oggv.tx audio pipeline" }, \
   { "pipeline:oggT.rx", FWDSP_OGGV_RX, "Default oggT.rx audio pipeline" }, \
   { "pipeline:oggT.tx", FWDSP_OGGV_TX("audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true"), "Default oggT.tx audio pipeline" },

#endif

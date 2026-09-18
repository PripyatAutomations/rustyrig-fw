// Shared native audio defaults. Keep config/rrclient.cfg and config/rrserver.cfg in sync.
#ifndef FWDSP_DEFAULT_PIPELINES_H
#define FWDSP_DEFAULT_PIPELINES_H

#define FWDSP_DEFAULT_CODECS "pc16 g722 mu16 mu08 opus oggv"

#define FWDSP_AUDIO_PIPELINE_DEFAULTS \
   { "pipeline:pc16.rx", \
      "appsrc name=rx-src is-live=true format=time do-timestamp=true caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      " tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "volume name=rx-vol ! " \
      "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default pc16.rx audio pipeline" }, \
   { "pipeline:pc16.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true ! " \
      " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default pc16.tx audio pipeline" }, \
   { "pipeline:g722.rx", \
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
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default g722.rx audio pipeline" }, \
   { "pipeline:g722.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=160 do-timestamp=true ! " \
      " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "avenc_g722 bitrate=64000 ! " \
      " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default g722.tx audio pipeline" }, \
   { "pipeline:mu16.rx", \
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
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default mu16.rx audio pipeline" }, \
   { "pipeline:mu16.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true ! " \
      " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "mulawenc ! " \
      "audio/x-mulaw,rate=16000,channels=1 ! " \
      " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default mu16.tx audio pipeline" }, \
   { "pipeline:mu08.rx", \
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
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default mu08.rx audio pipeline" }, \
   { "pipeline:mu08.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true ! " \
      " audio/x-raw,format=S16LE,rate=8000,channels=1,layout=interleaved ! " \
      "tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "mulawenc ! " \
      "audio/x-mulaw,rate=8000,channels=1 ! " \
      " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default mu08.tx audio pipeline" }, \
   { "pipeline:oggv.rx", \
      "appsrc name=rx-src is-live=true format=bytes caps=application/ogg ! " \
      "oggdemux ! " \
      "vorbisdec ! " \
      "audioconvert ! " \
      "audioresample ! " \
      "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "volume name=rx-vol ! " \
      "pulsesink device=default name=rx-sink client-name=fwdsp-rx sync=false t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default oggv.rx audio pipeline" }, \
   { "pipeline:oggv.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 samplesperbuffer=320 do-timestamp=true ! " \
      "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "audioconvert ! " \
      "vorbisenc quality=0.3 ! " \
      "oggmux max-delay=20000000 max-page-delay=20000000 ! " \
      "appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default oggv.tx audio pipeline" }, \
   { "pipeline:opus.rx", \
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
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default opus.rx audio pipeline" }, \
   { "pipeline:opus.tx", \
      "audiotestsrc is-live=true wave=sine freq=600 do-timestamp=true ! " \
      " audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! " \
      "tee name=t  t. ! " \
      "queue max-size-buffers=2 leaky=downstream ! " \
      "opusenc audio-type=generic frame-size=20 bitrate=24000 bitrate-type=cbr ! " \
      " appsink name=tx-sink emit-signals=false sync=false max-buffers=8 drop=false  t. ! " \
      "queue max-size-buffers=4 leaky=downstream ! " \
      "appsink name=record-sink emit-signals=false sync=false max-buffers=4 drop=true", \
      "Default opus.tx audio pipeline" },

#endif

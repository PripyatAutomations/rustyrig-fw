gst-launch-1.0 autoaudiosrc \
   ! audioconvert ! audioresample \
   ! audio/x-raw,format=S16LE,channels=1,rate=16000 \
   ! shmsink socket-path=/tmp/rrws-capture wait-for-connection=false

gst-launch-1.0 shmsrc socket-path=/tmp/rrws-playback is-live=true \
   ! audio/x-raw,format=S16LE,channels=1,rate=16000 \
   ! autoaudiosink

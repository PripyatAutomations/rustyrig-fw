#!/bin/bash
# Example of starting fwdsp for the audio side of things

[ ! -z "$(pidof fwdsp.bin)" ] && killall -9 fwdsp.bin
[ -z "$PROFILE" ] && PROFILE=radio

#####################
# RX audio (mic in) #
#####################
### Use raw 16khz PCM
rx_pipeline="pulsesrc device=default do-timestamp=1 ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! queue ! fdsink fd=%d"
### Use 8khz mulaw PCM
#rx_pipeline="pulsesrc device=default do-timestamp=1 ! audio/x-raw,rate=8000,channels=1 ! mulawenc ! fdsink fd=%d"
### Use 16khz mulaw PCM
#rx_pipeline="pulsesrc device=default do-timestamp=1 ! audio/x-raw,rate=16000,channels=1 ! mulawenc ! fdsink fd=%d"

################
# TX Pipelines #
################
#tx_pipeline="fdsrc fd=%d ! audioconvert ! audioresample ! queue ! pulsesink device=default"
tx_pipeline="fdsrc fd=%s do-timestamp=1 ! audioconvert ! pulsesink device=default"
export GST_DEBUG=*:3
./build/${PROFILE}/fwdsp.bin -p "${rx_pipeline}" &
#./build/${PROFILE}/fwdsp.bin -p "${tx_pipeline}" -t &

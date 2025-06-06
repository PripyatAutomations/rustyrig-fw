#!/bin/bash
# Example of starting fwdsp for the audio side of things

[ ! -z "$(pidof fwdsp.bin)" ] && killall -9 fwdsp.bin
[ -z "$PROFILE" ] && PROFILE=radio

# RX audio (mic in)
#   rx_pipeline="pulsesrc device=default ! audioconvert ! audioresample ! opusenc ! oggmux ! queue ! fdsink fd=%d"
#   rx_pipeline="pulsesrc device=default ! audioconvert ! audioresample ! flacenc ! oggmux ! queue ! fdsink fd=%d"
   rx_pipeline="pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! queue ! fdsink fd=%d"
#   rx_pipeline="pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=44100,channels=1 ! flacenc ! queue ! fdsink fd=%d"
#   tx_pipepine="fdsrc fd=%d ! oggdemux ! flacdec ! audioconvert ! audioresample ! queue ! pulsesink device=default"

export GST_DEBUG=*:3
./build/${PROFILE}/fwdsp.bin -p "${rx_pipeline}" &
#./build/${PROFILE}/fwdsp.bin -p "${tx_pipeline}" -t &

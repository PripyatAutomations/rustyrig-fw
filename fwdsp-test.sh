#!/bin/bash
# Example of starting fwdsp for the audio side of things

[ ! -z "$(pidof fwdsp.bin)" ] && killall -9 fwdsp.bin

[ -z "$PROFILE" ] && PROFILE=radio

# RX audio (mic in)
#./build/${PROFILE}/fwdsp.bin -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! queue ! fdsink fd=%d" -c pcm 
#./build/${PROFILE}/fwdsp.bin -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=44100,channels=1 ! queue ! fdsink fd=%d" -c pcm -s 44100
#  -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! flacenc ! fdsink fd=%d" \

FORMAT="FLAC+OGG"

if [ "$FORMAT" == "FLAC+OGG" ]; then
   rx_pipeline="pulsesrc device=default ! audioconvert ! audioresample ! flacenc ! oggmux ! queue ! fdsink fd=%d"
   tx_pipepine="fdsrc fd=%d ! oggdemux ! flacdec ! audioconvert ! audioresample ! queue ! pulsesink device=default"
fi

./build/${PROFILE}/fwdsp.bin -p "${rx_pipeline}" -c flac -s 44100 &
./build/${PROFILE}/fwdsp.bin -p "${tx_pipeline}" -c flac -s 44100 -t &

#!/bin/bash
# Example of starting fwdsp for the audio side of things

[ -z "$PROFILE" ] && PROFILE=radio

# RX audio (mic in)
./build/${PROFILE}/fwdsp.bin -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! queue ! fdsink fd=%d" -c pcm 
#./build/${PROFILE}/fwdsp.bin -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=44100,channels=1 ! queue ! fdsink fd=%d" -c pcm -s 44100 -x


#!/bin/bash
# Example of starting fwdsp for the audio side of things

# RX audio (mic in)
./build/radio/fwdsp.bin -r -p "pulsesrc device=default ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1 ! queue ! fdsink fd=%d" -c pcm 

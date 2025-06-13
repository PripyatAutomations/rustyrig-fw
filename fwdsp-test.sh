#!/bin/bash
# Example of starting fwdsp for the audio side of things

[ ! -z "$(pidof fwdsp.bin)" ] && killall -9 fwdsp.bin
[ -z "$PROFILE" ] && PROFILE=radio
export GST_DEBUG=*:3
./build/${PROFILE}/fwdsp.bin -f fwdsp.cfg -c mu16
#./build/${PROFILE}/fwdsp.bin -f fwdsp.cfg -c mu16 -t

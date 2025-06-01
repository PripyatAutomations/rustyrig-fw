#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

[ ! -f build/${PROFILE}/firmware.bin ] && ./build.sh

[ ! -z "$(pidof firmware.bin)" ] && killall -9 firmware.bin
[ -z "$(pidof rigctld)" ] && ./ft891-rigctld.sh &
sleep 2
./fwdsp-test.sh &
./build/${PROFILE}/firmware.bin
./killall.sh

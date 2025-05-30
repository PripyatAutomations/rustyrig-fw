#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

[ ! -f build/${PROFILE}/firmware.bin ] && ./build.sh
./killall.sh
./ft891-rigctld.sh &
sleep 2
./fwdsp-test.sh &
./build/${PROFILE}/firmware.bin
./killall.sh

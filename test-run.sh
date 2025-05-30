#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

[ ! -f build/${PROFILE}/firmware.bin ] && ./build.sh
./killall.sh
./ft891-rigctld.sh &
sleep 1
./fwdsp-test.sh &
sleep 1
./build/${PROFILE}/firmware.bin
./killall.sh

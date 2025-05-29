#!/bin/bash
[ ! -f build/radio/firmware.bin ] && ./build.sh
./killall.sh
./ft891-rigctld.sh &
sleep 1
./fwdsp-test.sh &
sleep 1
./build/radio/firmware.bin

#!/bin/bash
./ft891-rigctld.sh &
sleep 2
./fwdsp-test.sh &
sleep 3
./build/radio/firmware.bin

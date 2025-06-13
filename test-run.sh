#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

[ ! -f build/${PROFILE}/firmware.bin ] && ./build.sh

[ ! -z "$(pidof firmware.bin)" ] && killall -9 firmware.bin
[ ! -z "$(pidof fwdsp.bin)" ] && killall -9 fwdsp.bin

# If user has rigctld running, try to use it instead...
if [ -z "$(pidof rigctld)" ]; then
   ./ft891-rigctld.sh &
   sleep 2
fi

./build/${PROFILE}/fwdsp.bin -f config/rrserver.cfg -c mu16 &

if [ "$1" == "gdb" ]; then
   gdb ./build/${PROFILE}/firmware.bin -ex 'run'
else
   ./build/${PROFILE}/firmware.bin
fi

# Kill firmware and fwdsp instances
./killall.sh

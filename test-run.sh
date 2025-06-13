#!/bin/bash
[ -z "$PROFILE" ] && PROFILE=radio

[ ! -f build/${PROFILE}/rrserver ] && ./build.sh

[ ! -z "$(pidof rrserver)" ] && killall -9 rrserver
[ ! -z "$(pidof fwdsp)" ] && killall -9 fwdsp

# If user has rigctld running, try to use it instead...
if [ -z "$(pidof rigctld)" ]; then
   ./ft891-rigctld.sh &
   sleep 2
fi

# Start up fwdsp
./build/${PROFILE}/fwdsp -f config/fwdsp.cfg -c mu16 -t &

sleep 2

if [ "$1" == "gdb" ]; then
   gdb ./build/${PROFILE}/rrserver -ex 'run'
else
   ./build/${PROFILE}/rrserver
fi

# Kill firmware and fwdsp instances
./killall.sh

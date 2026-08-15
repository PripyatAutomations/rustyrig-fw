#!/bin/bash
FWPID=$(pidof rrserver)
DSPPID=$(pidof fwdsp)

killall -11 rrserver fwdsp
sleep 3
killall -9 rrserver fwdsp
sleep 0.1
[ ! -z "${FWPID}" -o ! -z "${DSPPID}" ] && echo "Killed rrserver [${FWPID}] and fwdsp [${DSPPID}]"

# Remove pulseaudio loopbacks
which pactl && [ ! -z "$(pactl list short modules | grep rrloop | cut -f 1)" ] && ./tools/stop-pulse-loopback.sh

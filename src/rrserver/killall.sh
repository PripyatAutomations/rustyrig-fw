#!/bin/bash
FWPID=$(pidof rrserver)
DSPPID=$(pidof fwdsp)

killall -TERM rrserver
sleep 3
killall -9 rrserver fwdsp
sleep 0.1
[ ! -z "${FWPID}" -o ! -z "${DSPPID}" ] && echo "Killed rrserver [${FWPID}] and fwdsp [${DSPPID}]"
[ ! -z "$(pactl list short modules | grep rrloop | cut -f 1)" ] && ./stop-pulse-loopback.sh

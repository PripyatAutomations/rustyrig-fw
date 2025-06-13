#!/bin/bash
FWPID=$(pidof firmware.bin)
DSPPID=$(pidof fwdsp.bin)

killall -TERM firmware.bin
sleep 3
killall -9 firmware.bin fwdsp.bin
sleep 0.1
[ ! -z "${FWPID}" -o ! -z "${DSPPID}" ] && echo "Killed firmware [${FWPID}] and fwdsp [${DSPPID}]"

#!/bin/bash
PORT=/dev/ttyUSB0
#DEBUG=-vvvv

(
  stty -F ${PORT} 38400 cs8 -cstopb -parenb
  sleep 0.1
  printf '.........PS1;'
) > ${PORT}
sleep 1

rigctld -o -m 1036 -P RIG -r ${PORT} ${DEBUG}

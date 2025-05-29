#!/bin/bash
PORT=/dev/ttyUSB1
#rigctld -o -m 1 -vvvv
#DEBUG=-vvvv
(
  stty -F /dev/ttyUSB1 38400 cs8 -cstopb -parenb
  sleep 0.1
  printf '.........PS1;'
) > ${PORT}
sleep 1

rigctld -o -m 1036 -P RIG -r ${PORT} ${DEBUG}



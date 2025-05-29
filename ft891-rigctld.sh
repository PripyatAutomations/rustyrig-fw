#!/bin/bash
PORT=/dev/ttyUSB1
#rigctld -o -m 1 -vvvv
#DEBUG=-vvvv
rigctld -o -m 1036 -P RIG -r ${PORT} ${DEBUG}



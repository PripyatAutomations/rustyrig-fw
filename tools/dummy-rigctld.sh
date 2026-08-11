#!/bin/bash

DEBUG=-vvvv
FLRIG_PID=
RIGCTLD_PID=

cleanup() {
   trap - EXIT INT TERM

   echo
   echo "Stopping..."

   [ -n "$RIGCTLD_PID" ] && kill "$RIGCTLD_PID" 2>/dev/null
   [ -n "$FLRIG_PID" ] && kill "$FLRIG_PID" 2>/dev/null

   exit 0
}

trap cleanup EXIT INT TERM

if command -v flrig >/dev/null 2>&1; then
   flrig &
   FLRIG_PID=$!
   echo "flrig running at pid ${FLRIG_PID}"

   sleep 2

   rigctld -m 4 -o &
   RIGCTLD_PID=$!
else
   rigctld -m 1 -o &
   RIGCTLD_PID=$!
fi

echo "rigctld running at pid ${RIGCTLD_PID}"
echo
echo "Press CTRL-C to exit..."

while :; do
   sleep 3600
done

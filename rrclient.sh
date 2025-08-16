#!/bin/bash
set -e
VALGRIND_LOG="run/rrclient/valgrind.%p.log"
VALGRIND_OPTS="--leak-check=full --track-origins=yes"

# XXX: This should go away someday...
mkdir -p run/rrclient

if [ ! -f ~/.config/rrclient.cfg ]; then
   echo "No config found at ~/.config/rrclient.cfg, copy example? [N/y]"
   read line
   if [ "${line}" == "y" -o "${line}"== "Y" ]; then
      cp config/rrclient.cfg.example ~.config/rrclient.cfg
   else
      echo "Skipping!"
      exit 1
   fi
fi

case "$1" in
   gdb )
     gdb ./build/client/rrclient -ex run
     ;;

   valgrind)
     rm -f run/rrclient/valgrind.*.log
     valgrind ${VALGRIND_OPTS} --log-file="${VALGRIND_LOG}" ./build/client/rrclient
     ;;

   *)
     ./build/client/rrclient
     ;;
esac

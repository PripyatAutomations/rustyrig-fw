#!/bin/bash
set -e
VALGRIND_LOG="audit-logs/valgrind.rrclient.%p.log"
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
     gdb ./bin/rrclient -ex run
     ;;

   valgrind)
     rm -f audit-logs/valgrind.rrclient.*.log
     valgrind ${VALGRIND_OPTS} --log-file="${VALGRIND_LOG}" ./bin/rrclient
     ;;

   *)
     ./bin/rrclient
     ;;
esac

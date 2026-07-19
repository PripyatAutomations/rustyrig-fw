#!/bin/bash
set -e
VALGRIND_LOG="audit-logs/valgrind.rrgtk.%p.log"
VALGRIND_OPTS="--leak-check=full --track-origins=yes"

# XXX: This should go away someday...
mkdir -p run/rrgtk

if [ ! -f ~/.config/rrgtk.cfg ]; then
   echo "No config found at ~/.config/rrgtk.cfg, copy example? [N/y]"
   read line
   if [ "${line}" == "y" -o "${line}" == "Y" ]; then
      cp config/rrgtk.cfg.example ~/.config/rrgtk.cfg
   else
      echo "Skipping!"
      exit 1
   fi
fi

case "$1" in
   gdb )
     gdb ./bin/rrgtk -ex run
     ;;

   valgrind)
     rm -f audit-logs/valgrind.rrgtk.*.log
     valgrind ${VALGRIND_OPTS} --log-file="${VALGRIND_LOG}" ./bin/rrgtk
     ;;

   *)
     ./bin/rrgtk
     ;;
esac

#!/bin/bash
set -e
VALGRIND_LOG="audit-logs/valgrind.rrclient.%p.log"
VALGRIND_OPTS="--leak-check=full --track-origins=yes --show-leak-kinds=all"

# XXX: This should go away someday...
mkdir -p run/rrclient

#if [ ! -f ~/.config/rrclient.cfg ]; then
#   echo "No config found at ~/.config/rrclient.cfg, copy example? [N/y]"
#   read line
#   if [ "${line}" == "y" -o "${line}" == "Y" ]; then
#      cp config/rrclient.cfg.example ~/.config/rrclient.cfg
#   else
#      echo "Skipping!"
#      exit 1
#   fi
#fi

#while [ ! -z $1 ]; do
   case "$1" in
      gtk)
        G_DEBUG=fatal-criticals gdb --ex run --args ./bin/rrclient
        ;;
      gdb)
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
#   shift
#done

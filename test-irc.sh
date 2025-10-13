#!/bin/bash
set -e

[ ! -x bin/rrcli ] && ./build.sh

rm -f /tmp/rrcli.log rrcli.log

case "$1" in
   gdb )
     gdb ./bin/rrcli -ex run
     ;;

   valgrind)
     rm -f audit-logs/valgrind.rrcli.*.log
     valgrind ${VALGRIND_OPTS} --log-file="${VALGRIND_LOG}" ./bin/rrcli
     ;;

   *)
     ./bin/rrcli
     ;;
esac


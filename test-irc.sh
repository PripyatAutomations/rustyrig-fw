#!/bin/bash
set -e

[ ! -x bin/irc-test ] && ./build.sh

case "$1" in
   gdb )
     gdb ./bin/irc-test -ex run
     ;;

   valgrind)
     rm -f audit-logs/valgrind.irc-test.*.log
     valgrind ${VALGRIND_OPTS} --log-file="${VALGRIND_LOG}" ./bin/irc-test
     ;;

   *)
     ./bin/irc-test
     ;;
esac


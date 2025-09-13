#!/bin/bash
set -e

for i in fwdsp rrclient rrserver; do
   OLDPWD=$(pwd)
   cd $i
   rm -f compile_commands.json
   make distclean
   echo "* Generating compile_commands.json"
   bear -- make
   cd "${OLDPWD}"
done

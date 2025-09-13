#!/bin/bash
set -e

for i in src/rrserver src/rrclient src/fwdsp; do
   OLDPWD=$(pwd)
   cd $i
   rm -f compile_commands.json
   make distclean
   echo "* Generating compile_commands.json"
   bear -- make
   cd "${OLDPWD}"
done

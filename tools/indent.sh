#!/bin/bash
#C_SRC=$(find . -name \*.\[ch\] | grep -v mongoose | grep -v '^./ext/')

#echo "${C_SRC}"

TOPLEVEL=$(pwd)
for i in fwdsp librrprotocol librustyaxe modsrc/* rrclient rrserver; do
   P=$(pwd)
   cd $i
   uncrustify -c ${TOPLEVEL}/.uncrustify.cfg *.[ch] --replace --no-backup
   cd ${TOPLEVEL}
done

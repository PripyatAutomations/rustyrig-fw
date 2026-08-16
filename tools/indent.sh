#!/bin/bash
#C_SRC=$(find . -name \*.\[ch\] | grep -v mongoose | grep -v '^./ext/')

#echo "${C_SRC}"

subdirs := librustyaxe librrprotocol fwdsp rrclient rrserver

TOPLEVEL=$(pwd)
for i in ${subdirs}; do
   P=$(pwd)
   cd $i
   uncrustify -c ${TOPLEVEL}/.uncrustify.cfg *.[ch] --replace --no-backup
   cd ${TOPLEVEL}
done

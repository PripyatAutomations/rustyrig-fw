#!/bin/bash
P=$(pwd)

for i in librustyaxe librrprotocol www callsign-lookup .; do
   cd $i
   echo "PUSH: $i"
   if [ -z "$1" ]; then
      git commit -a
   else
      git commit -a -m "${1}"
   fi
   git push
   cd "${P}"
done

#!/bin/bash
[ -z "$PROFILE" ] && [ ! -z "$1" ] && PROFILE=$1
[ -z "$PROFILE" ] && PROFILE=radio
ts=$(date +%Y-%m-%d.%H%M)

ofch=config/${PROFILE}.channels.json
ofcf=config/${PROFILE}.config.json
nfch=config/archive/${PROFILE}.channels.$ts.json
nfcf=config/archive/${PROFILE}.config.$ts.json

if [ ! -f "$ofcf" ]; then
   echo "Old config $ofcf doesnt exist!"
   exit 1
fi

cp "$ofcf" "$nfcf"
echo "* Archived config to $nfcf"

if [ -f "$ofch" ]; then
   cp "$ofch" "$nfch"
   echo "* Archived channels to $nfch"
else
   echo "* No channel memories in $ofch, skipping!"
fi

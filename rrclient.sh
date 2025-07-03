#!/bin/bash
set -e

if [ ! -f ~/.config/rrclient.cfg ]; then
   echo "No config found at ~/.config/rrclient.cfg, copy example? [N/y]"
   read line
   if [ "${line}" == "y" -o "${line}"== "Y" ]; then
      cp config/rrclient.cfg.example ~.config/rrclient.cfg
   else
      echo "Skipping!"
      exit 1
   fi
fi

[ -f ~/.config/rrclient.cfg ] && ./build/client/rrclient

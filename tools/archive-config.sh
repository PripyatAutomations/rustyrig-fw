#!/bin/bash
[ -z "$PROFILE" ] && [ ! -z "$1" ] && PROFILE=$1
[ -z "$PROFILE" ] && PROFILE=radio
ts=$(date +%Y-%m-%d.%H%M)

old_chans=config/${PROFILE}.channels.json
old_config=config/${PROFILE}.config.json
old_rrclient=config/rrclient.cfg
old_rrserver=config/rrserver.cfg
new_chans=config/archive/${PROFILE}.channels.$ts.json
new_config=config/archive/${PROFILE}.config.$ts.json
new_rrclient=config/archive/rrclient.$ts.cfg
new_rrserver=config/archive/rrserver.$ts.cfg

mkdir -p config/archive

#############################################################
# The radio config is essential to build, so it must exist! #
#############################################################
if [ ! -f "$old_config" ]; then
   echo "Old config $old_config doesnt exist!"
   exit 1
fi

cp "$old_config" "$new_config"
echo "* Archived config to $new_config"

#############################################################
# All this stuff is optional and make not exist in the tree #
#############################################################
if [ -f "$old_chans" ]; then
   cp "$old_chans" "$new_chans"
   echo "* Archived channels to $new_chans"
else
   echo "* No channel memories in $old_chans, skipping!"
fi

if [ -f "$old_rrclient" ]; then
   cp "$old_rrclient" "$new_rrclient"
   echo "* Archived client config to $new_rrclient"
else
   echo "* No client config in $old_rrclient, skipping!"
fi

if [ -f "$old_rrserver" ]; then
   cp "$old_rrserver" "$new_rrserver"
   echo "* Archived server config to $new_rrserver"
else
   echo "* No server config in $old_rrserver, skipping!"
fi

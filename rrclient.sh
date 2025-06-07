#!/bin/bash

[ -z "${PROFILE}" ] && PROFILE=client
make -C gtk-client

# Check if a valid config exists anywhere we look, if not, create one at ~/.

if ! [ -f "$HOME/.config/rrclient.cfg" ] &&
   ! [ -f "$HOME/.rrclient.cfg" ] &&
   ! [ -f "/etc/rrclient.cfg" ] &&
   ! [ -f "/opt/rustyrig/etc/rrclient.cfg" ]; then
    echo "Currently there is no rrclient.cfg - Do you want me to copy the example there then invoke \$EDITOR ($EDITOR) on it?"
    echo "(y/N)"
    read i

    if [ -z "$i" ]; then
       echo "No answer, bailing. Please manually create a config file"
       exit 1
    fi

    if [ "$i" == "y" -o "$i" == "Y" ]; then
       example="doc/rrclient.cfg.example"
       dst="$HOME/.config/rrclient.cfg"

       echo "Copying $example $dst"
       cp -i "$example" "$dst"

       if [ ! -z "${EDITOR}" ]; then
          ${EDITOR} "$dst"
       else
          echo "No \$EDITOR set. You'll need to manually edit $dst"
       fi
    fi
fi

#export GST_DEBUG=appsrc:5,gstbase:5,gstogg:5,gstflac:5
#export GST_DEBUG=appsrc:7
#export GST_DEBUG=*:3
#GST_DEBUG_FILE=debug.log 
./build/${PROFILE}/rrclient

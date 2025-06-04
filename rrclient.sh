#!/bin/bash

[ -z "${PROFILE}" ] && PROFILE=client
make -C gtk-client
#export GST_DEBUG=appsrc:5,gstbase:5,gstogg:5,gstflac:5
export GST_DEBUG=appsrc:7
#GST_DEBUG_FILE=debug.log 
./build/${PROFILE}/rrclient

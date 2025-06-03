#!/bin/bash

[ -z "${PROFILE}" ] && PROFILE=client
make -C gtk-client
./build/${PROFILE}/rrclient

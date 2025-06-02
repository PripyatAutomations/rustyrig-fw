#!/bin/bash

[ -z "${PROFILE}" ] && PROFILE=client
./build/${PROFILE}/rrclient

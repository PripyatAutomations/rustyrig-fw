#!/bin/bash
# Set a default profile (used to find configs, set output folder, etc), if none set
[ -z "$PROFILE" ] && PROFILE=radio

# Force a clean rebuild if rrserver or fwdsp missing
[ ! -f build/${PROFILE}/rrserver -o ! -f build/${PROFILE}/fwdsp ] && ./build.sh

# Stop rrsever and fwddsp, if running
[ ! -z "$(pidof fwdsp)" ] && killall -9 fwdsp
[ ! -z "$(pidof rrserver)" ] && killall -9 rrserver

# If user has rigctld running, try to use it instead of starting our own
if [ -z "$(pidof rigctld)" ]; then
   # Nope, lets see if they configured it
   if [ -f "config/rrserver.cfg" ]; then
      RIGCTLD_SCRIPT=$(grep rigctld.start-script config/rrserver.cfg | cut -f 2 -d '=')
      if [ ! -z "${RIGCTLD_SCRIPT}" ]; then
         if [ "$(basename ${RIGCTLD_SCRIPT})" == "${RIGCTLD_SCRIPT}" ]; then
            ./${RIGCTLD_SCRIPT} &
         else
            ${RIGCTLD_SCRIPT} &
         fi
      fi
   fi
#   ./ft891-rigctld.sh &
   ./dummy-rigctld.sh &
   sleep 2
fi

# Start up fwdsp
./build/${PROFILE}/fwdsp -c pc16 -t &
sleep 2
echo "*****************************"
sleep 2

if [ "$1" == "gdb" ]; then
   gdb ./build/${PROFILE}/rrserver -ex 'run'
else
   ./build/${PROFILE}/rrserver
fi

# Kill firmware and fwdsp instances
./killall.sh

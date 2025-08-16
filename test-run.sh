set -e

VALGRIND_LOG="run/rrserver/valgrind.%p.log"
VALGRIND_OPTS="--leak-check=full --track-origins=yes"

# Set a default profile (used to find configs, set output folder, etc), if none set
[ -z "$PROFILE" ] && PROFILE=radio

# Force a clean rebuild if rrserver or fwdsp missing
[ ! -f build/${PROFILE}/rrserver -o ! -f build/${PROFILE}/fwdsp ] && ./build.sh

# Stop rrsever and fwddsp, if running
[ ! -z "$(pidof fwdsp)" ] && killall -9 fwdsp
[ ! -z "$(pidof rrserver)" ] && killall -9 rrserver
#[ -z "$(pactl list short modules | grep rrloop | cut -f 1)" ] && ./start-pulse-loopback.sh

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
   sleep 2
fi

case "$1" in
   gdb)
      gdb ./build/${PROFILE}/rrserver -ex 'run'
      ;;
   valgrind)
      valgrind $VALGRIND_OPTS --log-file="$VALGRIND_LOG" ./build/${PROFILE}/rrserver
      ;;
   *)
      ./build/${PROFILE}/rrserver
      ;;
esac

# Kill firmware/fwdsp instances & tear down pulse rrloop devices
./killall.sh

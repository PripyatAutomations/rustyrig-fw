rrserver := bin/rrserver
bins += ${rrserver}

rrserver_objs += au.o			# core audio stuff
rrserver_objs += au.pipe.o		# audio over pipes (for gstreamer)
rrserver_objs += au.recording.o		# support for recording session audio
rrserver_objs += amp.o			# Support for amplifiers and their control
rrserver_objs += atu.o			# Support for auto-tuners and their control
rrserver_objs += backend.o		# Interface to various backends
rrserver_objs += backend.dummy.o	# Dummy (NOOP) backend for testing
rrserver_objs += backend.hamlib.o	# Hamlib backend for posix hosts
rrserver_objs += backend.internal.o	# Internal backend for real radios (rustyrig-fw)
rrserver_objs += channels.o		# Channel Memories
rrserver_objs += console.o		# Console support
rrserver_objs += database.o		# sqlite3 database stuff
rrserver_objs += defconfig.o		# Default configuration
rrserver_objs += events.o		# Our event hooks
rrserver_objs += faults.o		# Fault management/alerting
rrserver_objs += filters.o		# Support for managing BPF/LPF/HPF
rrserver_objs += gpio.o			# GPIO controls
rrserver_objs += gui.o			# Support for a GUI on the OLED/Nextion (NYI)
rrserver_objs += gui.fb.o		# Virtual framebuffer for GUI (NYI)
rrserver_objs += gui.nextion.o		# Nextion display support (NYI)
rrserver_objs += help.o			# support for help menus from filesystem, if available
rrserver_objs += i2c.o			# Support for i2c bus devices
rrserver_objs += main.o			# main loop
rrserver_objs += mqtt.o			# MQTT client/server support
rrserver_objs += network.o		# Network management/config for embedded hosts
rrserver_objs += protection.o		# Protection features
rrserver_objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
rrserver_objs += thermal.o		# Thermal management
rrserver_objs += timer.o		# Timers support
rrserver_objs += unwind.o		# Support for stack unwinding on crashes
rrserver_objs += webcam.o		# Support for v4l2 webcam on linux
#rrserver_objs += 

CFLAGS_RRSERVER += -I./modsrc/

rrserver_real_objs := $(foreach x, ${rrserver_objs}, ${OBJ_DIR}/rrserver/${x})
extra_clean += ${rrserver_real_objs}

${OBJ_DIR}/rrserver/%.o: rrserver/%.c ${BUILD_HEADERS} GNUmakefile rrserver/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRSERVER} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/rrserver: ${EEPROM_FILE} ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrserver_real_objs} ${MASTER_DB}
	@echo "[link] $< => $@"
	@${CC}  -o $@ ${rrserver_real_objs} -lrustyaxe -lrrprotocol -lev ${LDFLAGS} ${LDFLAGS_RRSERVER} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

rrserver-deps:

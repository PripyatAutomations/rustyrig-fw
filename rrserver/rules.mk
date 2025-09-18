rrserver_objs += amp.o			# Amplifier management
rrserver_objs += atu.o			# Antenna Tuner
rrserver_objs += au.o			# Audio channel stuff
rrserver_objs += au.recording.o		# Support for recording audio to files
rrserver_objs += auth.o			# User management
rrserver_objs += backend.o		# Support for multiple backends by setting up pointer into appropriate one
rrserver_objs += backend.dummy.o		# Dummy backend (not implemented yet - use hamlib + rigctld in dummy rig mode!)
rrserver_objs += backend.hamlib.o	# Support for using hamlib to control legacy rigs
rrserver_objs += backend.internal.o	# Internal (real hardware) backend
rrserver_objs += channels.o		# Channel Memories
rrserver_objs += console.o		# Console support
rrserver_objs += database.o		# sqlite3 database stuff
rrserver_objs += dds.o		# API for Direct Digital Synthesizers
rrserver_objs += dds.ad9833.o		# AD9833 DDS
rrserver_objs += dds.ad9959_stm32.o	# STM32 (AT command) ad9851 DDS
rrserver_objs += dds.si5351.o		# Si5351 synthesizer
rrserver_objs += defconfig.o		# Default configuration
rrserver_objs += eeprom.o		# "EEPROM" configuration storage
rrserver_objs += faults.o		# Fault management/alerting
rrserver_objs += filters.o		# Control of input/output filters
rrserver_objs += fwdsp-mgr.o		# fwdsp manager
rrserver_objs += gnuradio.o		# Support for GNU Radio (NYI)
rrserver_objs += gpio.o		# GPIO controls
rrserver_objs += gui.o		# Support for a local user-interface
rrserver_objs += gui.fb.o		# Generic LCD (framebuffer) interface
rrserver_objs += gui.nextion.o	# Nextion HMI display interface
rrserver_objs += help.o		# support for help menus from filesystem, if available
rrserver_objs += http.o		# HTTP server
rrserver_objs += http.api.o		# HTTP REST API
rrserver_objs += http.bans.o		# Support banning nuisance user-agents
rrserver_objs += i2c.o		# i2c abstraction
rrserver_objs += i2c.linux.o
rrserver_objs += i2c.mux.o		# i2c multiplexor support
rrserver_objs += main.o			# main loop
#rrserver_objs += mqtt.o			# Support for MQTT via mongoose
rrserver_objs += network.o		# Network control
rrserver_objs += protection.o		# Protection features
rrserver_objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
rrserver_objs += radioberry.o		# Radioberry device support
rrserver_objs += thermal.o		# Thermal management
rrserver_objs += timer.o			# Timers support
rrserver_objs += usb.o			# Support for USB control (stm32)
rrserver_objs += util.vna.o		# Vector Network Analyzer
rrserver_objs += unwind.o		# support for libunwind for stack tracing
rrserver_objs += vfo.o			# VFO control/management
rrserver_objs += waterfall.o		# Support for rendering waterfalls
rrserver_objs += webcam.o		# Webcam (v4l2) streaming to a canvas
rrserver_objs += ws.o			# Websocket transport general
rrserver_objs += ws.audio.o		# Audio (raw / OPUS) over websockets
rrserver_objs += ws.bcast.o		# Broadcasts over websocket (chat, rig status, etc)
rrserver_objs += ws.chat.o		# Websocket Chat (talk)
rrserver_objs += ws.rigctl.o		# Websocket Rig Control (CAT)

rrserver_real_objs := $(foreach x, ${rrserver_objs}, ${OBJ_DIR}/rrserver/${x})

${OBJ_DIR}/rrserver/%.o: rrserver/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRSERVER} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/rrserver: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${rrserver_real_objs} ${MASTER_DB}
	@${CC}  -o $@ ${rrserver_real_objs} -lrustyaxe -lmongoose ${LDFLAGS} ${LDFLAGS_RRSERVER} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

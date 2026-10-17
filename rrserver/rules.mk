CFLAGS_RRSERVER += -pthread
LDFLAGS_RRSERVER += -pthread -Wl,--export-dynamic
CFLAGS_RRSERVER += -DRRSERVER -DCHANNEL_FILE="\"config/${PROFILE}.channels.json\""
rrserver := bin/rrserver
rrserver_objs += files.o objects.o serial.o gps.o hardware.o hardware.dummy.o cfg.radio.o backend.internal-parts.o cfg.gpio.o si5351.o hardware.si5351.o cfg.si5351.o
ifeq ($(USE_GPIO),true)
ifneq ($(OPTIONAL_SKIP_PREFLIGHT),true)
ifeq ($(shell $(PKG_CONFIG) --atleast-version=2.0 libgpiod && echo yes),)
$(error GPIO support requires libgpiod >= 2.0 development files; install libgpiod-dev or disable features.gpio)
endif
endif
${BUILD_DIR}/rrserver/gpio.o: CFLAGS_RRSERVER += -DUSE_GPIO $(shell $(PKG_CONFIG) --cflags libgpiod)
LDFLAGS_RRSERVER += $(shell $(PKG_CONFIG) --libs libgpiod)
endif
# Rebuild the adapter and relink when switching the feature without cleaning.
.PHONY: rrserver-gpio-config-check
${BUILD_DIR}/rrserver/gpio.config: rrserver-gpio-config-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(USE_GPIO)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
${BUILD_DIR}/rrserver/gpio.o bin/rrserver: ${BUILD_DIR}/rrserver/gpio.config
extra_clean += ${BUILD_DIR}/rrserver/gpio.config
bins += ${rrserver}

rrserver_headers += $(wildcard rrserver/*.h)
rrserver_src = $(addprefix rrserver/,$(rrserver_objs:.o=.c))

#rrserver_objs += au.o			# core abstractions
#rrserver_objs += au.pcm5102.o           # Support for TI PCM5102 i2c DAC
rrserver_objs += au.recording.o		# support for recording session audio
rrserver_objs += amp.o			# Support for amplifiers and their control
rrserver_objs += atu.o			# Support for auto-tuners and their control
rrserver_objs += audit.o		# Store LOG_AUDIT Log() messages in the audit_log db table
rrserver_objs += backend.o		# Interface to various backends
rrserver_objs += backend.worker.o       # Per-instance hardware workers and main-thread completions
rrserver_objs += backend.instance.o	# Allocated backend instance lifecycle
rrserver_objs += backend.register.o	# Build-selected backend type registration
rrserver_objs += backend.internal.o	# Internal backend for real radios (rustyrig-fw)
rrserver_objs += channels.o		# Channel Memories
rrserver_objs += console.o		# Console support
rrserver_objs += usage.o supervision.o
rrserver_objs += database.rooms.o database.users.o database.accounting.o
rrserver_objs += database.o		# sqlite3 database stuff
rrserver_objs += defconfig.o		# Default configuration
rrserver_objs += acl.o
rrserver_objs += bridge.hints.o
rrserver_objs += bridge.access.o
rrserver_objs += bridge.manager.o
rrserver_objs += cfg.peer.o		# Peer configuration sections
rrserver_objs += relay.catalog.o relay.media.o relay.sessions.o relay.users.o
rrserver_objs += relay.o
rrserver_objs += link.o		# Authenticated server-link sessions
rrserver_objs += link.tree.o		# Server-link tree admission and routing
rrserver_objs += events.rooms.o events.rig.o events.quota.o events.users.o events.admin.o
rrserver_objs += events.o		# Our event hooks
rrserver_objs += faults.o		# Fault management/alerting
rrserver_objs += filters.o		# Support for managing BPF/LPF/HPF
rrserver_objs += gpio.o			# GPIO controls
rrserver_objs += hostlog.o		# Stream host Log() lines to FLAG_SYSLOG clients as binframes
rrserver_objs += help.o			# support for help menus from filesystem, if available
rrserver_objs += http.bans.o
rrserver_objs += i2c.o			# Support for i2c bus devices
rrserver_objs += main.o			# main loop
rrserver_objs += media.direct.o cfg.rtp.o
rrserver_objs += media.o		# media channel provisioning
rrserver_objs += rx.slice.o		# shared canonical RX slice identities
rrserver_objs += mqtt.o			# MQTT client/server support
rrserver_objs += network.o		# Network management/config for embedded hosts
rrserver_objs += protection.o		# Protection features
rrserver_objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
rrserver_objs += rig.compat.o		# Default-rig cat.state adapter
rrserver_objs += rig.rooms.o		# Per-rig room and media bindings
rrserver_objs += cfg.rig.o		# Named rig configuration sections/views
rrserver_objs += rig.properties.o	# Per-rig typed property state/control
rrserver_objs += rig.registry.o		# UUID-addressed runtime rig collection
rrserver_objs += thermal.o		# Thermal management
rrserver_objs += timer.o		# Timers support
rrserver_objs += timer.clocktick.o	# Our 1hz timer
rrserver_objs += unwind.o		# Support for stack unwinding on crashes
rrserver_objs += waterfall.o		# support for sending a waterfall
rrserver_objs += webcam.o		# Support for v4l2 webcam on linux

###########################
# GUI on the radio itself #
###########################
#rrserver_objs += gui.o			# Support for a GUI on the OLED/Nextion (NYI)
#rrserver_objs += gui.fb.o		# Virtual framebuffer for GUI (NYI)
#rrserver_objs += gui.nextion.o		# Nextion display support (NYI)
#rrserver_objs +=

rrserver_real_objs := $(foreach x, ${rrserver_objs}, ${BUILD_DIR}/rrserver/${x})
extra_clean += ${rrserver_real_objs}

${BUILD_DIR}/rrserver/%.o: rrserver/%.c ${rrserver_headers} ${BUILD_HEADERS} GNUmakefile rrserver/rules.mk ${librustyaxe_headers} ${librrprotocol_headers} ${BUILD_DIR}/build_config.h ${OBJECT_ORDER_ONLY}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRSERVER} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/rrserver: ${EEPROM_FILE} ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrserver_real_objs} ${MASTER_DB} ${libfwdspmgr} ${rrserver_src}
	@echo "[link] $@ from $(words ${rrserver_real_objs}) objects"
	@${CC}  -o $@ ${rrserver_real_objs} -lrustyaxe -lrrprotocol -lfwdspmgr ${LDFLAGS} ${LDFLAGS_RRSERVER} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

rrserver-deps:

###########
# Modules #
###########
# Receiver adapters use the existing loadable-module lifecycle and event bus.
gps_modules := bin/rrserver-gps-nmea.so
# XXX: This is mongoose based but needs thats fixed asap
gps_modules += bin/rrserver-gpsd.so
bins += ${gps_modules}
bin/rrserver-gps-nmea.so: ${BUILD_DIR}/rrserver/module.gps-nmea.o ${librustyaxe} ${librrprotocol}
	@${CC} ${LIB_LDFLAGS} -o $@ $< -lrustyaxe -lrrprotocol ${LDFLAGS}
bin/rrserver-gpsd.so: ${BUILD_DIR}/rrserver/module.gpsd.o ${librustyaxe} ${librrprotocol}
	@${CC} ${LIB_LDFLAGS} -o $@ $< -lrustyaxe -lrrprotocol ${LDFLAGS}
extra_clean += ${BUILD_DIR}/rrserver/module.gps-nmea.o ${BUILD_DIR}/rrserver/module.gpsd.o ${gps_modules}

# Hamlib is an optional backend module; never link it into the server/core libs.
ifeq (${USE_HAMLIB},true)
hamlib_module := bin/rrserver-hamlib.so
bins += ${hamlib_module}
${BUILD_DIR}/rrserver/backend.hamlib.o: CFLAGS_RRSERVER += $(shell pkg-config --cflags hamlib)
bin/rrserver-hamlib.so: ${BUILD_DIR}/rrserver/module.hamlib.o ${BUILD_DIR}/rrserver/backend.hamlib.o ${librustyaxe} ${librrprotocol}
	@${CC} ${LIB_LDFLAGS} -o $@ ${BUILD_DIR}/rrserver/module.hamlib.o ${BUILD_DIR}/rrserver/backend.hamlib.o -lrustyaxe -lrrprotocol $(shell pkg-config --libs hamlib) ${LDFLAGS}
endif
extra_clean += ${BUILD_DIR}/rrserver/backend.hamlib.o ${BUILD_DIR}/rrserver/module.hamlib.o bin/rrserver-hamlib.so

# Standalone preview/explicit hardware bring-up tool; no radio backend required.
bins += bin/rr-si5351
bin/rr-si5351: ${BUILD_DIR}/rrserver/si5351-tool.o ${BUILD_DIR}/rrserver/si5351.o ${BUILD_DIR}/rrserver/hardware.si5351.o ${BUILD_DIR}/rrserver/hardware.o ${librustyaxe}
	@${CC} -o $@ $(filter %.o,$^) -lrustyaxe ${LDFLAGS} || exit 1
extra_clean += ${BUILD_DIR}/rrserver/si5351-tool.o bin/rr-si5351

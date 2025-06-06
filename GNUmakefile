SHELL = bash
.SHELLFLAGS = -e -c
all: world

PROFILE ?= radio
CF := config/${PROFILE}.config.json
CHANNELS := config/${PROFILE}.channels.json
BUILD_DIR := build/${PROFILE}
OBJ_DIR := ${BUILD_DIR}/obj
#INSTALL_DIR = /opt/rustyrig
INSTALL_DIR = /usr/local
fw_bin := ${BUILD_DIR}/firmware.bin
fwdsp_bin := ${BUILD_DIR}/fwdsp.bin

bins += ${fw_bin} ${fwdsp_bin}

# Throw an error if the .json configuration file doesn't exist...
ifeq (x$(wildcard ${CF}),x)
$(error ***ERROR*** Please create ${CF} first before building -- There is an example at doc/radio.json.example you can use)
endif


CFLAGS := -std=gnu11 -g -ggdb -O1 -std=gnu99 -DMG_ENABLE_IPV6=1
CFLAGS_WARN := -Wall -Wno-unused -pedantic -Werror
LDFLAGS := -lc -lm -g -ggdb -lcrypt

CFLAGS += -I. -I${BUILD_DIR} -I${BUILD_DIR}/include $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
CFLAGS += -DLOGFILE="\"$(strip $(shell cat ${CF} | jq -r '.debug.logfile'))\""

ifneq (x${DEBUG_PROTO},x)
CFLAGS += -DDEBUG_PROTO
endif

LDFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.ldflags"))
TC_PREFIX := $(strip $(shell cat ${CF} | jq -r ".build.toolchain.prefix"))
EEPROM_SIZE := $(strip $(shell cat ${CF} | jq -r ".eeprom.size"))
EEPROM_FILE := ${BUILD_DIR}/eeprom.bin
PLATFORM := $(strip $(shell cat ${CF} | jq -r ".build.platform"))
USE_ALSA = $(strip $(shell cat ${CF} | jq -r '.features.alsa'))
USE_GSTREAMER = $(strip $(shell cat ${CF} | jq -r '.features.gstreamer'))
USE_HAMLIB = $(strip $(shell cat ${CF} | jq -r '.backend.hamlib'))
USE_LIBUNWIND = $(strip $(shell cat ${CF} | jq -r ".features.libunwind"))
USE_PIPEWIRE = $(strip $(shell cat ${CF} | jq -r '.features.pipewire'))
USE_OPUS = $(strip $(shell cat ${CF} | jq -r '.features.opus'))
USE_SQLITE = $(strip $(shell cat ${CF} | jq -r '.features.sqlite'))
USE_SSL = $(strip $(shell cat ${CF} | jq -r ".net.http.tls_enabled"))
USE_GTK = $(strip $(shell cat ${CF} | jq -r ".features.gtk"))

ifeq (${USE_GTK},true)
bins += build/client/rrclient
endif

FWDSP_CFLAGS += -D__FWDSP

ifeq (${USE_LIBUNWIND},true)
CFLAGS += -fno-omit-frame-pointer -Og -gdwarf -DUSE_LIBUNWIND
LDFLAGS += -lunwind
ifeq ($(shell uname -m),x86_64)
LDFLAGS += -lunwind-x86_64
endif

endif

ifeq (${USE_HAMLIB},true)
LDFLAGS += -lhamlib
endif

ifeq (${USE_OPUS},true)
LDFLAGS += $(shell pkg-config --libs opus)
CFLAGS += $(shell pkg-config --cflags opus)
endif

ifeq (${USE_SSL},true)
CFLAGS += -DMG_TLS=MG_TLS_MBED
LDFLAGS += -lmbedcrypto -lmbedtls -lmbedx509
endif

ifeq (${USE_SQLITE},true)
CFLAGS += -DUSE_SQLITE
LDFLAGS += -lsqlite3
MASTER_DB := $(strip $(shell cat ${CF} | jq -r ".database.master.path"))
endif

ifeq (${USE_GSTREAMER},true)
FWDSP_CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
FWDSP_LDFLAGS += $(shell pkg-config --libs gstreamer-1.0)
endif

# Are we cross compiling?
ifneq (${TC_PREFIX},"")
CC := ${TC_PREFIX}-gcc
LD := ${TC_PREFIX}-ld
else
CC := gcc
LD := ld
endif

##################
# Source objects #
##################
fw_objs += amp.o			# Amplifier management
fw_objs += atu.o			# Antenna Tuner
fw_objs += au.o			# Audio channel stuff
fw_objs += au.pipe.o		# pipe / socket support
fw_objs += au.pcm5102.o		# pcm5102 i2s DAC support

fw_objs += au.recording.o		# Support for recording audio to files
fw_objs += auth.o			# User management
fw_objs += backend.o		# Support for multiple backends by setting up pointer into appropriate one
fw_objs += backend.dummy.o		# Dummy backend (not implemented yet - use hamlib + rigctld in dummy rig mode!)
fw_objs += backend.hamlib.o	# Support for using hamlib to control legacy rigs
fw_objs += backend.internal.o	# Internal (real hardware) backend
fw_objs += cat.o			# CAT parsers
fw_objs += cat.kpa500.o		# amplifier control (KPA-500 mode)
fw_objs += cat.yaesu.o		# Yaesu CAT protocol
fw_objs += channels.o		# Channel Memories
fw_objs += codec.o			# Support for audio codec
fw_objs += console.o		# Console support
fw_objs += database.o		# sqlite3 database stuff
fw_objs += dds.o			# API for Direct Digital Synthesizers
fw_objs += dds.ad9833.o		# AD9833 DDS
fw_objs += dds.ad9959_stm32.o	# STM32 (AT command) ad9851 DDS
fw_objs += dds.si5351.o		# Si5351 synthesizer
fw_objs += debug.o			# Debug stuff
fw_objs += dict.o			# dictionary object
fw_objs += eeprom.o		# "EEPROM" configuration storage
fw_objs += faults.o		# Fault management/alerting
fw_objs += filters.o		# Control of input/output filters
fw_objs += gpio.o			# GPIO controls
fw_objs += gui.o			# Support for a local user-interface
fw_objs += gui.fb.o		# Generic LCD (framebuffer) interface
fw_objs += gui.nextion.o		# Nextion HMI display interface
#fw_objs += gui.h264.o		# Framebuffer via H264 (over http)
fw_objs += help.o			# support for help menus from filesystem, if available
fw_objs += http.o			# HTTP server
fw_objs += http.api.o		# HTTP REST API
fw_objs += i2c.o			# i2c abstraction
fw_objs += i2c.linux.o
fw_objs += i2c.mux.o		# i2c multiplexor support
fw_objs += io.o			# Input/Output abstraction/portability
fw_objs += io.serial.o		# Serial port stuff
fw_objs += io.socket.o		# Socket operations
fw_objs += logger.o		# Logging facilities
fw_objs += main.o			# main loop
fw_objs += mongoose.o		# Mongoose http/websocket/mqtt library
fw_objs += mqtt.o			# Support for MQTT via mongoose
fw_objs += network.o		# Network control

ifeq (${USE_SQLITE},true)
CFLAGS += $(shell pkg-config --cflags sqlite3)
LDFLAGS += $(shell pkg-config --libs sqlite3)
endif
ifeq (${PLATFORM},posix)
fw_objs += posix.o			# support for POSIX hosts (linux or perhaps others)
LDFLAGS += -lgpiod
endif

fw_objs += power.o			# Power monitoring and management
fw_objs += protection.o		# Protection features
fw_objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
fw_objs += radioberry.o		# Radioberry device support
fw_objs += thermal.o		# Thermal management
fw_objs += timer.o			# Timers support
fw_objs += usb.o			# Support for USB control (stm32)
fw_objs += util.file.o		# Misc file functions
fw_objs += util.math.o		# Misc math functions
fw_objs += util.string.o		# String utility functions
fw_objs += util.vna.o		# Vector Network Analyzer
fw_objs += unwind.o		# support for libunwind for stack tracing
fw_objs += vfo.o			# VFO control/management
fw_objs += waterfall.o		# Support for rendering waterfalls
fw_objs += webcam.o		# Webcam (v4l2) streaming to a canvas
fw_objs += ws.o			# Websocket transport general
fw_objs += ws.audio.o		# Audio (raw / OPUS) over websockets
fw_objs += ws.bcast.o		# Broadcasts over websocket (chat, rig status, etc)
fw_objs += ws.chat.o		# Websocket Chat (talk)
fw_objs += ws.rigctl.o		# Websocket Rig Control (CAT)

##### DSP #####
ifeq (${USE_ALSA},true)
fwdsp_objs += fwdsp.alsa.o		# ALSA on posix hosts
endif

ifeq (${USE_PIPEWIRE},true)
fwdsp_objs += fwdsp.pipewire.o		# Pipewire on posix hosts
CFLAGS += $(shell pkg-config --cflags libpipewire-0.3)
LDFLAGS += $(shell pkg-config --libs libpipewire-0.3)
endif
fwdsp_objs += fwdsp-main.o
fwdsp_objs += logger.o
fwdsp_objs += posix.o
fwdsp_objs += util.file.o

# translate unprefixed object file names to source file names
fw_src = $(fw_objs:.o=.c)
fwdsp_src = $(fwdsp_objs:.o:.c)

# prepend objdir path to each object
real_fw_objs := $(foreach x, ${fw_objs}, ${OBJ_DIR}/firmware/${x})

# prepend objdir path to each object
real_fwdsp_objs := $(foreach x, ${fwdsp_objs}, ${OBJ_DIR}/fwdsp/${x})

################################################################################
###############
# Build Rules #
###############
# Remove log files, etc
extra_clean += firmware.log
# Remove autogenerated headers on clean
extra_clean += $(wildcard ${BUILD_DIR}/*.h)

# Extra things to build/clean:
extra_build += ${EEPROM_FILE}
extra_clean += ${EEPROM_FILE}

world: ${extra_build} ${bins}

${bins}: ${CF}

BUILD_HEADERS=${BUILD_DIR}/build_config.h ${BUILD_DIR}/eeprom_layout.h $(wildcard inc/*.h) $(wildcard ${BUILD_DIR}/*.h)
${OBJ_DIR}/firmware/%.o: %.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@echo "[compile] $@ from $<"
	@${RM} -f $@
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

# fwdsp
${OBJ_DIR}/fwdsp/%.o: %.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@echo "[compile] $@ from $<"
	@${RM} -f $@
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $< || exit 1

# Binary also depends on the .stamp file
${fw_bin}: ${real_fw_objs} ext/libmongoose/mongoose.c config/http.users ${fw_src}
	@echo "[Link] firmware ($@) from $(words ${real_fw_objs}) object files..."
	@${CC} -o $@ ${real_fw_objs} ${LDFLAGS} || exit 1
	@ls -a1ls $@
	@file $@
	@size $@

# Binary also depends on the .stamp file
${fwdsp_bin}: ${real_fwdsp_objs} ${fwdsp_src}
	@echo "[Link] fwdsp ($@) from $(words ${real_fwdsp_objs}) object files..."
	${CC} -o $@ ${real_fwdsp_objs} ${LDFLAGS} ${FWDSP_LDFLAGS} || exit 1
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${fw_bin} ${fw_dspbin}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

${BUILD_DIR}/build_config.h ${EEPROM_FILE} buildconf: ${CF} ${CHANNELS} $(wildcard res/*.json) buildconf.pl
	@echo "[buildconf]"
	set -e; ./buildconf.pl ${PROFILE}

##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bins} ${real_fwdsp_objs} ${real_fw_objs} ${extra_clean}
	${MAKE} -C gtk-client $@

distclean: clean
	@echo "[distclean]"
	${RM} -r build
	${RM} -f config/archive/*.json *.log state/*
	${MAKE} -C gtk-client $@

install:
#	@echo "Automatic DFU installation isn't supported yet... Please see doc/INSTALLING.txt for more info"
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	cp -av ${bins}  ${INSTALL_DIR}/bin
	@${MAKE} -C gtk-client install
#	cp -av archive-config.sh *-rigctld.sh fwdsp-test.sh killall.sh rrclient.sh test-run.sh ${INSTALL_DIR}/bin
#	cp -aiv config/${PROFILE}.*.json config/client.config.json ${INSTALL_DIR}/etc
 
###################
# Running on host #
###################
ifeq (${PLATFORM},posix)
# Run debugger
run: ${MASTER_DB} ${EEPROM_FILE} ${fw_bin}
#	@echo "[run] ${fwdsp_bin} & ${fw_bin}"
#	${fwdsp_bin} &
#	${fw_bin}
	@echo "*** Running test script at test-run.sh ***"
	./test-run.sh

gdb debug: ${fw_bin} ${EEPROM_FILE}
	@echo "[gdb] ${fw_bin}"
	@gdb ${fw_bin} -ex 'run'

test: clean world run
endif

#########################
# Rebuild a clean image #
#########################
rebuild clean-build cleanbuild: distclean buildconf world

#################
# Configuration #
#################
res/bandplan.json:
	exit 1

res/eeprom_layout.json:
	exit 1

res/eeprom_types.json:
	exit 1

# Display an error message and halt the build, if no configuration file
${CF}:
	@echo "********************************************************"
	@echo "* PLEASE read README.txt and edit ${CF} as needed *"
	@echo "********************************************************"
	exit 1

installdep:
	@./install-deps.sh

update:
	git pull
	git submodule init
	git submodule update --remote --recursive

recent-diff:
	git diff '@{6 hours ago}'

ext/libmongoose/mongoose.c:
	@echo "You forgot to git submodule init; git submodule update. Doing it for you!"
	git submodule init
	git submodule update

config/http.users:
	@echo "*** You do not have a config/http.users so i've copied the default file from doc/"
	cp doc/http.users.example config/http.users

${MASTER_DB}:
	mkdir -p $(shell dirname "${MASTER_DB}")
	sqlite3 $@ < sql/sqlite.master.sql

dump-ptt:
	echo -e ".headers on\nselect * from ptt_log order BY start_time;" | sqlite3 ${MASTER_DB}

dump-log:
	echo -e ".headers on\nselect * from audit_log;" | sqlite3 ${MASTER_DB}

build/client/rrclient: build/client/build_config.h
	${MAKE} -C gtk-client


build/client/build_config.h:
	./buildconf.pl client

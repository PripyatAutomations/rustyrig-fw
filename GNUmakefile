SHELL = bash
.SHELLFLAGS = -e -c
all: world

PROFILE ?= radio
CF := config/${PROFILE}.config.json
CHANNELS := config/${PROFILE}.channels.json
BUILD_DIR := build/${PROFILE}
OBJ_DIR := ${BUILD_DIR}/obj
bin := ${BUILD_DIR}/firmware.bin

# Throw an error if the .json configuration file doesn't exist...
ifeq (x$(wildcard ${CF}),x)
$(error ***ERROR*** Please create ${CF} first before building -- There is an example at doc/radio.json.example you can use)
endif

CFLAGS := -std=gnu11 -g -O1 -Wall -Wno-unused -pedantic -std=gnu99
LDFLAGS := -lc -lm -g -lcrypt

CFLAGS += -I${BUILD_DIR} -I${BUILD_DIR}/include $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
CFLAGS += -DLOGFILE="\"$(strip $(shell cat ${CF} | jq -r '.debug.logfile'))\""
LDFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.ldflags"))
TC_PREFIX := $(strip $(shell cat ${CF} | jq -r ".build.toolchain.prefix"))
EEPROM_SIZE := $(strip $(shell cat ${CF} | jq -r ".eeprom.size"))
EEPROM_FILE := ${BUILD_DIR}/eeprom.bin
PLATFORM := $(strip $(shell cat ${CF} | jq -r ".build.platform"))
USE_ALSA = $(strip $(shell cat ${CF} | jq -r '.features.alsa'))
USE_PIPEWIRE = $(strip $(shell cat ${CF} | jq -r '.features.pipewire'))
USE_HAMLIB = $(strip $(shell cat ${CF} | jq -r '.backend.hamlib'))
USE_OPUS = $(strip $(shell cat ${CF} | jq -r '.features.opus'))
USE_SSL = $(strip $(shell cat ${CF} | jq -r ".net.http.tls_enabled"))

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

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
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
objs += amp.o			# Amplifier management
objs += atu.o			# Antenna Tuner
objs += audio.o			# Audio channel stuff
objs += audio.pcm5102.o		# pcm5102 DAC support
objs += backend.o		# Support for multiple backends by setting up pointer into appropriate one
objs += backend.dummy.o		# Dummy backend (not implemented yet - use hamlib + rigctld in dummy rig mode!)
objs += backend.hamlib.o	# Support for using hamlib to control legacy rigs
objs += backend.internal.o	# Internal (real hardware) backend
objs += cat.o			# CAT parsers
objs += cat.kpa500.o		# amplifier control (KPA-500 mode)
objs += cat.yaesu.o		# Yaesu CAT protocol
objs += channels.o		# Channel Memories
objs += codec.o			# Support for audio codec
objs += console.o		# Console support
objs += crc32.o			# CRC32 calculation for eeprom verification
objs += dds.o			# API for Direct Digital Synthesizers
objs += dds.ad9833.o		# AD9833 DDS
objs += dds.ad9959_stm32.o	# STM32 (AT command) ad9851 DDS
objs += dds.si5351.o		# Si5351 synthesizer
objs += debug.o			# Debug stuff
objs += dict.o			# dictionary object
objs += eeprom.o		# "EEPROM" configuration storage
objs += faults.o		# Fault management/alerting
objs += filters.o		# Control of input/output filters
objs += gpio.o			# GPIO controls
objs += gui.o			# Support for a local user-interface
objs += gui.fb.o		# Generic LCD (framebuffer) interface
objs += gui.nextion.o		# Nextion HMI display interface
#objs += gui.h264.o		# Framebuffer via H264 (over http)
objs += help.o			# support for help menus from filesystem, if available
objs += http.o			# HTTP server
objs += i2c.o			# i2c abstraction
objs += i2c.mux.o		# i2c multiplexor support
objs += io.o			# Input/Output abstraction/portability
objs += logger.o		# Logging facilities
objs += main.o			# main loop
objs += mongoose.o		# Mongoose http/websocket/mqtt library
objs += mqtt.o			# Support for MQTT via mongoose
objs += network.o		# Network control

ifeq (${PLATFORM}, posix)
objs += gui.mjpeg.o		# Framebuffer via MJPEG streaming (over http)
ifeq (${USE_PIPEWIRE},true)
objs += audio.pipewire.o	# Pipwiere on posix hosts
CFLAGS += $(shell pkg-config --cflags libpipewire-0.3 libjpeg sqlite3)
LDFLAGS += $(shell pkg-config --libs libpipewire-0.3 libjpeg sqlite3)
endif
objs += posix.o			# support for POSIX hosts (linux or perhaps others)
endif

objs += power.o			# Power monitoring and management
objs += protection.o		# Protection features
objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
objs += serial.o		# Serial port stuff
objs += socket.o		# Socket operations
objs += thermal.o		# Thermal management
objs += timer.o			# Timers support
objs += usb.o			# Support for USB control (stm32)
objs += util.vna.o		# Vector Network Analyzer
objs += vfo.o			# VFO control/management
objs += waterfall.o		# Support for rendering waterfalls
objs += websocket.o		# Websocket transport for CAT and audio
objs += ws.auth.o		# Websocket Authentication
objs += ws.chat.o		# Websocket Chat (talk)
objs += ws.rigctl.o		# Websocket Rig Control (CAT)

# translate unprefixed object file names to source file names
src_files = $(objs:.o=.c)

# prepend objdir path to each object
real_objs := $(foreach x, ${objs}, ${OBJ_DIR}/${x})

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

world: ${extra_build} ${bin}

BUILD_HEADERS=${BUILD_DIR}/build_config.h ${BUILD_DIR}/eeprom_layout.h $(wildcard *.h) $(wildcard ${BUILD_DIR}/*.h)
${OBJ_DIR}/%.o: %.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<
	@echo "[compile] $@ from $<"

# Binary also depends on the .stamp file
${bin}: ${real_objs} ext/libmongoose/mongoose.c config/http.users
	@${CC} -o $@ ${real_objs} ${LDFLAGS}
	@echo "[Link] $@ from $(words ${real_objs}) object files..."
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${bin}
	@echo "[strip] ${bin}"
	@strip ${bin}
	@ls -a1ls ${bin}

${BUILD_DIR}/build_config.h ${EEPROM_FILE} buildconf: ${CF} ${CHANNELS} $(wildcard res/*.json) buildconf.pl
	@echo "[buildconf]"
	set -e; ./buildconf.pl ${PROFILE}

##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bin} ${real_objs} ${extra_clean}

distclean: clean
	@echo "[distclean]"
	${RM} -r build
	${RM} -f config/archive/*.json *.log

###############
# DFU Install #
###############
install:
	@echo "Automatic DFU installation isn't supported yet... Please see doc/INSTALLING.txt for more info"

###################
# Running on host #
###################
ifeq (${PLATFORM},posix)
# Run debugger
run: ${EEPROM_FILE} ${bin}
	@echo "[run] ${bin}"
	@${bin}

gdb debug: ${bin} ${EEPROM_FILE}
	@echo "[gdb] ${bin}"
	@gdb ${bin} -ex 'run'

test: clean world run
endif

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

ext/libmongoose/mongoose.c:
	@echo "You forgot to git submodule init; git submodule update. Doing it for you!"
	git submodule init
	git submodule update

config/http.users:
	@echo "*** You do not have a config/http.users so i've copied the default file from doc/"
	cp doc/http.users.example config/http.users

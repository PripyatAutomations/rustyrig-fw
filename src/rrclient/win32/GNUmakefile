SHELL = bash
.SHELLFLAGS = -e -c
all: world
PROFILE := win32-client
BUILD_DIR := ./build
OBJ_DIR := ${BUILD_DIR}/obj

UNAME_S := $(shell uname -s)

CC := x86_64-w64-mingw32-gcc
LD := x86_64-w64-mingw32-gcc
export PKG_CONFIG_PATH := /mingw64/lib/pkgconfig/

bin := ${BUILD_DIR}/rrclient.exe
CFLAGS := -std=gnu11 -g -ggdb -O1 -DMG_ENABLE_IPV6=1 -DVERSION="\"$(shell cat ../.version)\"" -lws2_32 -DMG_ARCH=MG_ARCH_WIN32
CFLAGS_WARN := -Wall -Wno-unused -pedantic #-Werror
LDFLAGS := -lm -g -ggdb

CFLAGS += -I../../../inc -I${BUILD_DIR} -I${BUILD_DIR}/include -DLOGFILE="\"rrclient.log\"" -D__RRCLIENT=1
CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS +=
LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
LDFLAGS += $(shell pkg-config --cflags --libs gstreamer-app-1.0)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
LDFLAGS += $(shell pkg-config --libs gstreamer-1.0)
CFLAGS += -DMG_TLS=MG_TLS_MBED
LDFLAGS += -lmbedcrypto -lmbedtls -lmbedx509

##################
# Source objects #
##################
objs += audio.o			# gstreamer based audio
objs += auth.o
objs += chat.o
objs += codecneg.o
objs += config.o
objs += debug.o			# Debug stuff
objs += defconfig.o
objs += dict.o			# dictionary object
objs += gtk.o                   # Support for a GTK user interface
objs += gtk.editcfg.o
objs += gtk.freqentry.o		# Frequency Entry Widget
objs += gtk.serverpick.o        # server picker
objs += gtk.syslog.o		# syslog tab
objs += logger.o		# Logging facilities
objs += main.o			# main loop
objs += mongoose.o		# Mongoose http/websocket/mqtt library
objs += posix.o			# support for POSIX hosts (linux or perhaps others)
objs += userlist.o
objs += util.file.o		# Misc file functions
objs += util.math.o		# Misc math functions
objs += util.string.o		# String utility functions
objs += win32.o			# support to run in windows
objs += ws.o			# Websocket transport general
objs += ws.audio.o		# Audio over websocket negotiation
objs += ws.auth.o
objs += ws.chat.o
objs += ws.rigctl.o
objs += ws.tx-audio.o

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
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_build += ${BUILD_DIR}/obj/.stamp

world: ${extra_build} ${bin}

BUILD_HEADERS=$(wildcard ../../inc/rrclient/*.h) $(wildcard ../../inc/common/*.h) $(wildcard ${BUILD_DIR}/*.h)

${OBJ_DIR}/%.o: ../%.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $@ from $<"
	${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/%.o: ../../common/%.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] shared $@ from $<"
	${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/mongoose.o: ../../../ext/libmongoose/mongoose.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@echo "[compile] mongoose from $<"
	${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/audio.o: ../audio.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<

# Binary also depends on the .stamp file
${bin}: ${real_objs} ../../../ext/libmongoose/mongoose.c
	@echo "[Link] $@ from $(words ${real_objs}) object files..."
	${CC} -o $@ ${real_objs} ${LDFLAGS}
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${bin}
	@echo "[strip] ${bin}"
	@strip ${bin}
	@ls -a1ls ${bin}

##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bin} ${real_objs} ${extra_clean}

distclean: clean
	@echo "[distclean]"
	${RM} -rf build

################
# Host Install #
################
install:
	@echo "Windows builds do not need to install. A NSIS installer will be generated soon"

###################
# Running on host #
###################
run: ${bin}
	@echo "[run] ${bin}"
	${bin}

# Run debugger
#gdb debug: ${bin}
#	@echo "[gdb] ${bin}"
#	@(cd ..; gdb gtk-client/${bin} -ex 'run')

test: clean world run

#########################
# Rebuild a clean image #
#########################
rebuild clean-build cleanbuild: distclean world

../../ext/libmongoose/mongoose.c:
	@echo "You forgot to git submodule init; git submodule update. Doing it for you!"
	git submodule init
	git submodule update

${BUILD_DIR}/obj/.stamp:
	mkdir -p "${BUILD_DIR}/obj/"
	touch $@

win32-deps:
	pacman -Syu
	pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base

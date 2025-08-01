all: host-info world
VERSION=$(shell cat ../../.version)

UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
   OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
   OS := MSYS
else
   OS := POSIX
endif

SHELL = bash
.SHELLFLAGS = -e -c

CFLAGS_WARN := -Wall -pedantic -Wno-unused #-Werror
CFLAGS += -std=gnu11 -g -ggdb -O1 -DMG_ENABLE_IPV6=1
CFLAGS += -I../../inc -I${BUILD_DIR} -I${BUILD_DIR}/include -DLOGFILE="\"rrclient.log\""
CFLAGS += -DMG_TLS=MG_TLS_MBED -D__RRCLIENT=1
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
# Do we want to spew excessive http debugging? This has a performance penalty...
#CFLAGS += -DHTTP_DEBUG_CRAZY=1
LDFLAGS += -lm -g -ggdb -lmbedcrypto -lmbedtls -lmbedx509
LDFLAGS += $(shell pkg-config --libs gtk+-3.0)
LDFLAGS += $(shell pkg-config --cflags --libs gstreamer-app-1.0)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
LDFLAGS += $(shell pkg-config --libs gstreamer-1.0)

# msys2 windows 64bit
ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
BUILD_DIR := ./build
PROFILE := win32-client
bin := ${BUILD_DIR}/rrclient.exe
CC := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LD := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LDFLAGS += -lws2_32 -luser32 -lkernel32
CFLAGS += -DMG_ARCH=MG_ARCH_WIN32
export PKG_CONFIG_PATH := /mingw64/lib/pkgconfig/
real_install := windows-install
objs += icon.res
else
# POSIX compliant hosts
PROFILE ?= client
BUILD_DIR := ../../build/${PROFILE}
INSTALL_DIR := /usr/local
bin := ${BUILD_DIR}/rrclient
CC := gcc
LD := ld
CFLAGS += -DMG_ARCH=MG_ARCH_UNIX
LDFLAGS += -lc -lcrypt
real_install := posix-install
endif

OBJ_DIR := ${BUILD_DIR}/obj

host-info:
	@echo "* Building on ${UNAME_S}"

##################
# Source objects #
##################
include ../../mk/rrclient.objs.mk
# translate unprefixed object file names to source file names
src_files = $(objs:.o=.c)
# prepend objdir path to each object
real_objs := $(foreach x, ${objs}, ${OBJ_DIR}/${x})

###############
# Build Rules #
###############
# Remove log files, etc
extra_clean += firmware.log
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_build += ${BUILD_DIR}/obj/.stamp

world: ${extra_build} ${bin}

BUILD_HEADERS=$(wildcard ../inc/rrclient/*.h) $(wildcard ../inc/common/*.h) $(wildcard ${BUILD_DIR}/*.h)
${OBJ_DIR}/%.o: %.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $@ from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/%.o: ../common/%.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] shared $@ from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/mongoose.o: ../../ext/libmongoose/mongoose.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it...
	@${RM} -f $@
	@echo "[compile] mongoose from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/audio.o: audio.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	@${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<

${bin}: ${real_objs} ../../ext/libmongoose/mongoose.c
	@echo "[Link] $@ from $(words ${real_objs}) object files..."
	@${CC} -o $@ ${real_objs} ${LDFLAGS}
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
	${RM} -rf ${BUILD_DIR}

run: ${bin}
	@echo "[run] ${bin}"
	${bin}

gdb debug: ${bin}
	@echo "[gdb] ${bin}"
	gdb ${bin} -ex 'run'

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

###################
# install targets #
###################
windows-install:
	@echo "Windows builds do not need to install. A NSIS installer can be built using win64-installer target"

posix-install:
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	install -Dm755 ${bin} ${INSTALL_DIR}/bin/$(shell basename "${bin}")
	install -Dm644 rustyrig.png /usr/share/icons/hicolor/48x48/apps/rustyrig.png
	# Transform the .desktop shortcut to use $PREFIX
	sed rrclient.desktop -e "s%/usr/local/%${PREFIX}%g" > ..tmp.rrclient.desktop
	install -Dm644 ..tmp.rrclient.desktop /usr/share/applications/rrclient.desktop
	@${RM} -f ..tmp.rrclient.desktop

install: ${real_install}

#convert-icon:
#	convert radio.png -define icon:auto-resize=16,32,48,64,128,256 rustyrig.ico

#################
# Windows Build #
#################
${BUILD_DIR}/.stamp-win64dep: win64-deps

win64-deps:
	pacman -Syu
	pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base nsis
	touch ${BUILD_DIR}.stamp-win64dep

DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

win64-installer: ${INSTALLER} ${BUILD_DIR}/.stamp-win64dep

${INSTALLER}: ${bin}
	makensis //DOUTFILE=${INSTALLER} win32/rrclient.nsi

${OBJ_DIR}/icon.res: rustyrig.ico win32/icon.rc
	windres.exe win32/icon.rc -O coff -o $@

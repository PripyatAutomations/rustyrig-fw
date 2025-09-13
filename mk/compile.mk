UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
   OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
   OS := MSYS
else
   OS := POSIX
endif

USE_GTK=true


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
objs += icon.res

else

# POSIX compliant hosts
PROFILE ?= client
BUILD_DIR ?= build/${PROFILE}
INSTALL_DIR ?= /usr/local
CFLAGS += -DMG_ARCH=MG_ARCH_UNIX

endif

CF := config/${PROFILE}.config.json

ifeq (${USE_GTK},true)
CFLAGS += -DUSE_GTK=1 $(shell pkg-config --cflags gtk+-3.0)
gtk_ldflags += $(shell pkg-config --libs gtk+-3.0)
endif

UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
   OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
   OS := MSYS
else
   OS := POSIX
endif

#SHELL = bash
#.SHELLFLAGS = -e -c

OBJ_DIR := ${BUILD_DIR}/obj

CFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
CFLAGS += $(shell pkg-config --cflags mbedtls)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
CFLAGS += -I./ -I../ -I./inc -I${BUILD_DIR} -I${BUILD_DIR}/include
CFLAGS += -DMG_TLS=MG_TLS_MBED
CFLAGS += -DMG_ENABLE_IPV6=1
CFLAGS += -DHTTP_DEBUG_CRAZY=1
CFLAGS += -DDEBUG_WS_BINFRAMES=1
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
CFLAGS += -I${BUILD_DIR} -I${BUILD_DIR}/include 
CFLAGS += -DLOGFILE="\"$(strip $(shell cat ${CF} | jq -r '.debug.logfile'))\""
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
#CFLAGS += -DUSE_EEPROM

LDFLAGS += -L. -L./librustyaxe
LDFLAGS += -lc -lm -g -ggdb -lcrypt
LDFLAGS += $(shell pkg-config --libs mbedtls mbedcrypto mbedx509)
gst_ldflags += $(shell pkg-config --cflags --libs gstreamer-app-1.0)
gst_ldflags += $(shell pkg-config --libs gstreamer-1.0)

FWDSP_CFLAGS += -D__FWDSP
RRCLIENT_CFLAGS += -D__RRCLIENT=1

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

ifeq (${USE_SSL},true)
CFLAGS += -DMG_TLS=MG_TLS_MBED
LDFLAGS += -lmbedcrypto -lmbedtls -lmbedx509
endif

# Are we cross compiling?
ifneq (${TC_PREFIX},"")
CC := ${TC_PREFIX}-gcc
LD := ${TC_PREFIX}-ld
else
CC := gcc
LD := ld
endif

ifeq (${USE_SQLITE},true)
CFLAGS += $(shell pkg-config --cflags sqlite3)
LDFLAGS += $(shell pkg-config --libs sqlite3)
endif

ifneq (x${DEBUG_PROTO},x)
CFLAGS += -DDEBUG_PROTO
endif

host-info:
	@echo "* Building on ${UNAME_S}"

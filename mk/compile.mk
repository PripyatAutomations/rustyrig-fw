UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
OS := MSYS
else
OS := POSIX
endif

# msys2 windows 64bit
ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)

bin := ./rrclient.exe
CC := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LD := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LDFLAGS += -lws2_32 -luser32 -lkernel32
CFLAGS += -DMG_ARCH=MG_ARCH_WIN32
export PKG_CONFIG_PATH := /mingw64/lib/pkgconfig/
objs += icon.res

else

# POSIX compliant hosts
INSTALL_DIR ?= /usr/local
CFLAGS += -DMG_ARCH=MG_ARCH_UNIX

endif

ifeq (${USE_GTK},true)
CFLAGS += -DUSE_GTK=1 $(shell pkg-config --cflags gtk+-3.0)
gtk_ldflags += $(shell pkg-config --libs gtk+-3.0)
endif

SHELL = bash
.SHELLFLAGS = -e -c

CFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
CFLAGS += $(shell pkg-config --cflags mbedtls)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
CFLAGS += -I./ -I../ -I./inc
CFLAGS += -DMG_ENABLE_IPV6=1
CFLAGS += -DHTTP_DEBUG_CRAZY=1 -DDEBUG_WS_BINFRAMES=1
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
CFLAGS += -DLOGFILE="\"$(strip $(shell cat ${CF} | jq -r '.debug.logfile'))\""
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
#CFLAGS += -DUSE_EEPROM

LDFLAGS += -L. -L./librustyaxe -Wl,-rpath,.
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

fwdsp_real_objs := $(foreach x, ${fwdsp_objs}, ${OBJ_DIR}/fwdsp/${x})
rrclient_real_objs := $(foreach x, ${rrclient_objs}, ${OBJ_DIR}/rrclient/${x})
rrserver_real_objs := $(foreach x, ${rrserver_objs}, ${OBJ_DIR}/rrserver/${x})

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${fwdsp_real_objs}
	${CC} -o $@ ${fwdsp_real_objs} ${gst_ldflags} ${LDFLAGS} ${LDFLAGS_FWDSP} -lrustyaxe -lmongoose
	@ls -a1ls $@
	@file $@
	@size $@

bin/rrclient: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${rrclient_real_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_RRCLIENT} -o $@ ${rrclient_real_objs} -lrustyaxe -lmongoose ${gtk_ldflags} ${gst_ldflags}
	@ls -a1ls $@
	@file $@
	@size $@

bin/rrserver: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${rrserver_real_objs} ${MASTER_DB}
	${CC}  -o $@ ${rrserver_real_objs} -lrustyaxe -lmongoose ${LDFLAGS} ${LDFLAGS_RRSERVER}
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${bins}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

${OBJ_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/rrclient/%.o: rrclient/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/rrserver/%.o: rrserver/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

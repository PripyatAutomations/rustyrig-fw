UNAME_S := $(shell uname -s)
# Host RTP support is optional; ESP32 can provide its own SRTP adapter.
WITH_RTP ?= $(shell pkg-config --exists libsrtp2 && echo true || echo false)
WITH_RTP_ENCRYPTION ?= false
SRTP_CFLAGS ?= $(shell pkg-config --cflags libsrtp2 2>/dev/null)
SRTP_LIBS ?= $(shell pkg-config --libs libsrtp2 2>/dev/null)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
OS := MSYS
else
OS := POSIX
endif

# msys2 windows 64bit
ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)

bin := ./rrgtk.exe
CC := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LD := /mingw64/bin/x86_64-w64-mingw32-gcc.exe
LDFLAGS += -lws2_32 -luser32 -lkernel32
CFLAGS += -DMG_ARCH=MG_ARCH_WIN32
export PKG_CONFIG_PATH := /mingw64/lib/pkgconfig/
objs += ${OBJ_DIR}/icon.res

else

# POSIX compliant hosts
INSTALL_DIR ?= /usr
CFLAGS += -DMG_ARCH=MG_ARCH_UNIX

endif

# GTK is only used by the rrclient GTK frontend module (rrclient/rules.mk
# builds bin/rrgtk.so with GTK_CFLAGS). The core client never gets USE_GTK.
GTK_MODULE_CFLAGS := -DUSE_GTK=1 $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)

SHELL = bash
.SHELLFLAGS = -e -c

CFLAGS += -Wno-duplicated-branches
CFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
CFLAGS += -Wno-deprecated-declarations
# Prefer the checked-out dependency headers over system-installed copies.
CFLAGS += -I./ -I./librrprotocol -I./librustyaxe -I../ -I./inc -I${BUILD_DIR}
CFLAGS += -DMG_ENABLE_IPV6=1
CFLAGS += -DHTTP_DEBUG_CRAZY=1 -DDEBUG_WS_BINFRAMES=1
CFLAGS += -DCONFDIR="\"${CONF_DIR}\""
CFLAGS += -DHOST_EEPROM_FILE="\"build/${PROFILE}/eeprom.bin\""
# SANITIZE may be supplied on the make command line for interactive QC.
# Keep build.use_asan compatible with existing config profiles.
ifeq ($(USE_ASAN),true)
SANITIZE ?= address,undefined
endif
ifneq ($(strip $(SANITIZE)),)
CFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer -g -fno-common -fno-sanitize-recover=all
LDFLAGS += -fsanitize=$(SANITIZE)
endif

# Media targets remain unsanitized during client/server QC unless explicitly
# opted in. Shared core libraries may still be sanitizer-instrumented.
SANITIZE_MEDIA ?= false
ifeq ($(SANITIZE_MEDIA),true)
MEDIA_CFLAGS = $(CFLAGS)
MEDIA_LDFLAGS = $(LDFLAGS)
else
MEDIA_CFLAGS = $(filter-out -fsanitize=% -fno-sanitize-recover=all,$(CFLAGS))
MEDIA_LDFLAGS = $(filter-out -fsanitize=%,$(LDFLAGS))
endif

# Resolve shared libraries beside the executable/library or in its prefix,
# including bin/ programs running directly from the source tree.
LDFLAGS += -L. -L./librustyaxe -Wl,-rpath,'$$ORIGIN:$$ORIGIN/../lib:$$ORIGIN/..'
LDFLAGS += -lc -lm -g -ggdb -lcrypt -lbsd
# librustyaxe is linked in every host build, including headless rrserver.
# Its event loop / TUI needs glib-unix.h, g_unix_fd_add(), and GLib core.
# Resolve headers and link libraries together, independently of GTK/GStreamer.
# Keep these in the common flags: librustyaxe/rules.mk consumes CFLAGS when
# compiling its objects and LDFLAGS when linking librustyaxe.so.
GLIB_PACKAGES := glib-2.0
ifeq ($(shell pkg-config --exists $(GLIB_PACKAGES) && echo yes),)
$(error Missing $(GLIB_PACKAGES) development files; install libglib2.0-dev (or your platform equivalent) and pkg-config)
endif
GLIB_CFLAGS := $(shell pkg-config --cflags $(GLIB_PACKAGES))
GLIB_LIBS := $(shell pkg-config --libs $(GLIB_PACKAGES))
CFLAGS += $(GLIB_CFLAGS)
LDFLAGS += $(GLIB_LIBS)

ifeq (${USE_GSTREAMER},true)
gst_cflags += $(shell pkg-config --cflags gstreamer-app-1.0)
gst_ldflags += $(shell pkg-config --libs gstreamer-app-1.0)
endif

FWDSP_CFLAGS += -D__FWDSP
CFLAGS_RRCLI += -D__RRCLI=1

ifeq (${USE_LIBUNWIND},true)
CFLAGS += -fno-omit-frame-pointer -Og -gdwarf
LDFLAGS += -lunwind

ifeq ($(shell uname -m),x86_64)
LDFLAGS += -lunwind-x86_64
endif
endif


ifeq (${USE_SSL},true)
CFLAGS += -DMG_TLS=MG_TLS_MBED
LDFLAGS += -lmbedcrypto -lmbedtls -lmbedx509
endif

# Are we cross compiling?
# An empty toolchain prefix must select plain gcc/ld. A quoted
# empty string comparison chooses the cross-compiler branch and creates
# CC=-gcc, whose leading hyphen makes GNU Make ignore compiler errors.
ifneq ($(strip $(TC_PREFIX)),)
CC := $(TC_PREFIX)-gcc
LD := $(TC_PREFIX)-ld
else
CC := gcc
LD := ld
endif

# Use ccache if requested via config (build.ccache: true)
ifeq (${USE_CCACHE},true)
CC := ccache ${CC}
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

strip: ${bins}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

LIB_LDFLAGS := -fPIC -shared

ifeq (${USE_LIBMBEDTLS},true)
CFLAGS += $(shell pkg-config --cflags mbedtls)
LDFLAGS += $(shell pkg-config --libs mbedtls mbedcrypto mbedx509)
endif

# New and improved GNU makefile
# Compiler/linker failures must never be ignored. GNU Make's -i option can
# arrive implicitly through the environment's MAKEFLAGS or a parent make.
# Its short-option group is the first MAKEFLAGS word (e.g. "i", "ik").
# Reject it before any build recipes can run and leave stale artifacts behind.
ifneq ($(findstring i,$(filter-out --%,$(firstword $(MAKEFLAGS)))),)
$(error Refusing to build with make -i/--ignore-errors; remove it from MAKEFLAGS or the make command)
endif

.PHONY: world
all: world

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
include mk/install-paths.mk
include mk/optional-modules.mk
# Keep targets and SDK checks in agreement, including command-line opt-outs.
$(foreach m,$(OPTIONAL_MODULES),$(eval override $(OPTIONAL_$(m)_FEATURE) := $(OPTIONAL_$(m)_ENABLED)))
BUILD_DIR := ./build/${PROFILE}

# Build exclusively against installed development libraries.
AXE_PKG_CONFIG ?= pkg-config
RRPROTO_PKG_CONFIG ?= pkg-config
IRC_PKG_CONFIG ?= pkg-config
AXE_CC ?= $(if $(strip $(TC_PREFIX)),$(TC_PREFIX)-gcc,$(if $(filter command line environment environment override,$(origin CC)),$(CC),gcc))
AXE_PROBE := $(shell AXE_CC='$(AXE_CC)' AXE_PKG_CONFIG='$(AXE_PKG_CONFIG)' \
    AXE_CPPFLAGS='$(CPPFLAGS)' AXE_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
    AXE_LDFLAGS='$(LDFLAGS)' sh tools/probe-librustyaxe.sh)
RRPROTO_PROBE := $(shell RRPROTO_CC='$(AXE_CC)' RRPROTO_PKG_CONFIG='$(RRPROTO_PKG_CONFIG)' \
    RRPROTO_CPPFLAGS='$(CPPFLAGS)' RRPROTO_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
    RRPROTO_LDFLAGS='$(LDFLAGS)' sh tools/probe-librrprotocol.sh)
IRC_PROBE := $(shell IRC_CC='$(AXE_CC)' IRC_PKG_CONFIG='$(IRC_PKG_CONFIG)' \
    IRC_CPPFLAGS='$(CPPFLAGS)' IRC_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
    IRC_LDFLAGS='$(LDFLAGS)' sh tools/probe-librustyirc.sh)
ifeq ($(filter clean distclean uninstall gmatrix-clean,$(MAKECMDGOALS)),)
ifeq ($(AXE_PROBE),none)
$(error Installed librustyaxe development headers/library required)
endif
ifeq ($(RRPROTO_PROBE),none)
$(error Installed librrprotocol development headers/library required)
endif
ifeq ($(IRC_PROBE),none)
$(error Installed librustyirc development headers/library required)
endif
endif
# Never link against stale local copies from pre-split checkouts.
ifeq ($(filter clean distclean uninstall gmatrix-clean,$(MAKECMDGOALS)),)
ifneq ($(strip $(wildcard librustyaxe.so* librrprotocol.so* librustyirc.so*)),)
$(error Stale local shared libraries in checkout; run make distclean)
endif
endif
AXE_SYSTEM_CFLAGS := $(shell $(AXE_PKG_CONFIG) --cflags librustyaxe 2>/dev/null)
AXE_SYSTEM_LDFLAGS := $(shell $(AXE_PKG_CONFIG) --libs-only-L librustyaxe 2>/dev/null)
RRPROTO_SYSTEM_CFLAGS := $(shell $(RRPROTO_PKG_CONFIG) --cflags librrprotocol 2>/dev/null)
RRPROTO_SYSTEM_LDFLAGS := $(shell $(RRPROTO_PKG_CONFIG) --libs-only-L librrprotocol 2>/dev/null)
IRC_SYSTEM_CFLAGS := $(shell $(IRC_PKG_CONFIG) --cflags librustyirc 2>/dev/null)
IRC_SYSTEM_LDFLAGS := $(shell $(IRC_PKG_CONFIG) --libs-only-L librustyirc 2>/dev/null)

include mk/compile.mk
include mk/database.mk
include mk/eeprom.mk

extra_clean += $(wildcard ${BUILD_DIR}/*.h) $(wildcard */compile_commands.json)
extra_clean += ${EEPROM_FILE} librustyaxe.so librustyaxe.so.0 librrprotocol.so librrprotocol.so.0 librustyirc.so librustyirc.so.0
extra_clean += firmware.log
extra_clean_targets += gmatrix-clean
.PHONY: gmatrix-clean
gmatrix-clean:
	$(MAKE) -C gmatrix clean

# All builds need a generated configuration header; only embedded builds
# need EEPROM layout/types and a firmware image.
BUILD_HEADERS += ${BUILD_DIR}/build_config.h
ifeq ($(USE_EEPROM),true)
BUILD_HEADERS += ${BUILD_DIR}/eeprom_types.h ${BUILD_DIR}/eeprom_layout.h
endif
OBJECT_ORDER_ONLY := | ${EEPROM_GENERATED}
${BUILD_DIR}/%.o: | ${EEPROM_GENERATED}


# Callsign lookup is a separate upstream source/build/package.
include libfwdspmgr/rules.mk
include librrsdrutils/rules.mk
include rrbridge/rules.mk

ifeq (${BUILD_RRCLIENT},true)
include rrclient/rules.mk
endif

ifeq (${BUILD_RRSERVER},true)
include rrserver/rules.mk
endif

ifeq (${BUILD_FWDSP},true)
include fwdsp/rules.mk
endif

# The standalone pipeline editor is available with the GTK/GStreamer desktop
# dependencies and is built alongside the GTK client.
ifeq (${USE_GTK},true)
ifeq (${USE_GSTREAMER},true)
ifneq ($(OS),DARWIN)
include rrpipelines/rules.mk
endif
endif
endif

# Optional standalone GTK screensaver. Part of the ordinary build and
# install-bin artifact list, so 'make install' and Debian packaging agree.
ifneq ($(filter true,$(USE_GTK)),)
ifneq ($(OS),DARWIN)
bins += gmatrix/gmatrix
gmatrix/gmatrix: gmatrix/gmatrix.c gmatrix/gmatrix.h gmatrix/Makefile
	$(MAKE) -C gmatrix all
endif
endif

include mk/install.mk
include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk
include mk/debug.mk
include mk/resource.mk
include mk/packaging.mk

# Relink programs and libraries when their runtime search paths change.
${bins} ${libs}: mk/compile.mk


${BUILD_DIR}/.stamp:
	mkdir -p "${BUILD_DIR}"
	touch $@

world: after-config

# The codec/GPSP harness also runs automatically in make test through
# rrclient/tests/test_codec_commands.sh (run-tests.sh discovers test_*.sh).
.PHONY: test-rrclient-codecs
test-rrclient-codecs: ${bins}
	bash rrclient/tests/test_codec_commands.sh

# Audit RustyRig-owned files without modifying the sources. External libraries
# are maintained separately, and third-party assets are excluded.
.PHONY: license-check license-migrate
license-check:
	perl tools/releng-gplv2-headers.pl --check

license-migrate:
	perl tools/releng-gplv2-headers.pl

# Build configured programs and run the maintained project test suites.
TEST_SUITES ?= selftest packaging rrserver rrclient fwdsp libfwdspmgr
.PHONY: test
test tests: ${bins} test-rrsdrutils
	./tests/run-tests.sh ${TEST_SUITES}

# Keep the historical alias for scripts which invoke "after-eeprom".
.PHONY: after-config after-eeprom buildconf
after-config: ${BUILD_DIR}/build_config.h ${EEPROM_REQUIRED}
after-config: ${BUILD_DIR}/.stamp ${extra_build} ${bins}
after-eeprom: after-config
# Used by CI and packaging: generate build configuration without compiling.
buildconf: ${BUILD_DIR}/build_config.h ${EEPROM_REQUIRED}

audit-log:
	./tools/rr-get-audit-log

chat-log:
	./tools/rr-get-chat-log

ptt-log:
	./tools/rr-get-ptt-log

# Native programs and the child share the codec list and fallback pipelines.
${BUILD_DIR}/rrclient/defconfig.o ${BUILD_DIR}/rrserver/defconfig.o ${BUILD_DIR}/fwdsp/defconfig.o ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.o: fwdsp/default-pipelines.h

# Refresh tracked source-derived privilege catalogs for native and browser editors.
.PHONY: privileges
privileges:
	python3 tools/generate-privileges.py

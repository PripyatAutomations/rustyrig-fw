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

# Compiler-first external library selection. pkg-config is an optional source
# of flags, never a requirement and not an indication that a library is usable.
# The probe links a symbol against installed headers/libs outside this tree.
LIBRUSTYAXE_PROVIDER ?= auto
AXE_PKG_CONFIG ?= pkg-config
AXE_CC ?= $(if $(strip $(TC_PREFIX)),$(TC_PREFIX)-gcc,$(if $(filter command line environment environment override,$(origin CC)),$(CC),gcc))
AXE_PROBE := $(shell AXE_CC='$(AXE_CC)' AXE_PKG_CONFIG='$(AXE_PKG_CONFIG)' \
    AXE_CPPFLAGS='$(CPPFLAGS)' AXE_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
    AXE_LDFLAGS='$(LDFLAGS)' sh tools/probe-librustyaxe.sh)
ifeq ($(LIBRUSTYAXE_PROVIDER),auto)
LIBRUSTYAXE_PROVIDER := $(if $(filter none,$(AXE_PROBE)),local,system)
endif
ifeq ($(LIBRUSTYAXE_PROVIDER),system)
ifeq ($(AXE_PROBE),none)
$(error Cannot compile/link against installed librustyaxe; set LIBRUSTYAXE_PROVIDER=local for the submodule)
endif
librustyaxe :=
librustyaxe_headers :=
ifeq ($(AXE_PROBE),pkgconfig)
AXE_SYSTEM_CFLAGS := $(shell $(AXE_PKG_CONFIG) --cflags librustyaxe)
AXE_SYSTEM_LDFLAGS := $(shell $(AXE_PKG_CONFIG) --libs-only-L librustyaxe)
endif
# Never let an old in-tree SONAME supersede an installed one at runtime.
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifneq ($(wildcard librustyaxe.so librustyaxe.so.0),)
$(error Stale checkout-root librustyaxe.so; run make distclean before using system librustyaxe)
endif
endif
else ifeq ($(LIBRUSTYAXE_PROVIDER),local)
librustyaxe := librustyaxe.so
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifeq ($(wildcard librustyaxe/rules.mk),)
ifneq ($(wildcard .gitmodules),)
$(info [librustyaxe] initializing submodule for uninstalled development build)
$(shell git submodule update --init librustyaxe)
endif
endif
ifeq ($(wildcard librustyaxe/rules.mk),)
$(error Missing librustyaxe/rules.mk; run git submodule update --init librustyaxe)
endif
endif
else
$(error Invalid LIBRUSTYAXE_PROVIDER=$(LIBRUSTYAXE_PROVIDER); use auto, system or local)
endif

# librrprotocol follows the same compiler-first selection as librustyaxe.
# An installed protocol library is only used with an installed core library.
LIBRRPROTOCOL_PROVIDER ?= auto
RRPROTO_PKG_CONFIG ?= pkg-config
RRPROTO_CC ?= $(AXE_CC)
RRPROTO_PROBE := $(shell RRPROTO_CC='$(RRPROTO_CC)' RRPROTO_PKG_CONFIG='$(RRPROTO_PKG_CONFIG)' \
    RRPROTO_CPPFLAGS='$(CPPFLAGS)' RRPROTO_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
    RRPROTO_LDFLAGS='$(LDFLAGS)' sh tools/probe-librrprotocol.sh)
ifeq ($(LIBRRPROTOCOL_PROVIDER),auto)
ifeq ($(LIBRUSTYAXE_PROVIDER),local)
LIBRRPROTOCOL_PROVIDER := local
else
LIBRRPROTOCOL_PROVIDER := $(if $(filter none,$(RRPROTO_PROBE)),local,system)
endif
endif
ifeq ($(LIBRRPROTOCOL_PROVIDER),system)
ifeq ($(LIBRUSTYAXE_PROVIDER),local)
$(error System librrprotocol needs installed librustyaxe; do not mix with local librustyaxe)
endif
ifeq ($(RRPROTO_PROBE),none)
$(error Installed librrprotocol not linkable; set LIBRRPROTOCOL_PROVIDER=local)
endif
librrprotocol :=
librrprotocol_headers :=
ifeq ($(RRPROTO_PROBE),pkgconfig)
RRPROTO_SYSTEM_CFLAGS := $(shell $(RRPROTO_PKG_CONFIG) --cflags librrprotocol)
RRPROTO_SYSTEM_LDFLAGS := $(shell $(RRPROTO_PKG_CONFIG) --libs-only-L librrprotocol)
endif
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifneq ($(wildcard librrprotocol.so librrprotocol.so.0),)
$(error Stale local librrprotocol; run make distclean before selecting installed protocol library)
endif
endif
else ifeq ($(LIBRRPROTOCOL_PROVIDER),local)
librrprotocol := librrprotocol.so
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifeq ($(wildcard librrprotocol/rules.mk),)
$(info [librrprotocol] initializing submodule for uninstalled development build)
$(shell git submodule update --init librrprotocol)
endif
ifeq ($(wildcard librrprotocol/rules.mk),)
$(error Missing librrprotocol/rules.mk; initialize submodule)
endif
endif
else
$(error Invalid LIBRRPROTOCOL_PROVIDER=$(LIBRRPROTOCOL_PROVIDER); use auto, system or local)
endif
# IRC is maintained inside RustyAxe, but is a separate GPLv2 shared
# library. Installed IRC takes priority; local fallback is the Axe submodule.
LIBRUSTYIRC_PROVIDER ?= auto
IRC_PKG_CONFIG ?= pkg-config
IRC_CC ?= $(AXE_CC)
IRC_PROBE := $(shell IRC_CC='$(IRC_CC)' IRC_PKG_CONFIG='$(IRC_PKG_CONFIG)' \
  IRC_CPPFLAGS='$(CPPFLAGS)' IRC_CFLAGS='$(CFLAGS) $(shell pkg-config --cflags glib-2.0 2>/dev/null)' \
  IRC_LDFLAGS='$(LDFLAGS)' sh tools/probe-librustyirc.sh)
ifeq ($(LIBRUSTYIRC_PROVIDER),auto)
LIBRUSTYIRC_PROVIDER := $(if $(filter none,$(IRC_PROBE)),local,system)
endif
ifeq ($(LIBRUSTYIRC_PROVIDER),system)
ifeq ($(IRC_PROBE),none)
$(error Installed librustyirc not usable; select local fallback or install the IRC SDK)
endif
librustyirc :=
ifeq ($(IRC_PROBE),pkgconfig)
IRC_SYSTEM_CFLAGS := $(shell $(IRC_PKG_CONFIG) --cflags librustyirc)
IRC_SYSTEM_LDFLAGS := $(shell $(IRC_PKG_CONFIG) --libs-only-L librustyirc)
endif
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifneq ($(wildcard librustyirc.so librustyirc.so.0),)
$(error Stale local librustyirc; run make distclean before system mode)
endif
endif
else ifeq ($(LIBRUSTYIRC_PROVIDER),local)
librustyirc := librustyirc.so
ifeq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
ifeq ($(wildcard librustyaxe/librustyirc/rules.mk),)
$(shell git submodule update --init librustyaxe)
endif
ifeq ($(wildcard librustyaxe/librustyirc/rules.mk),)
$(error Missing RustyAxe IRC source; initialize librustyaxe submodule)
endif
endif
else
$(error Invalid LIBRUSTYIRC_PROVIDER=$(LIBRUSTYIRC_PROVIDER))
endif

include mk/compile.mk
include mk/database.mk
include mk/eeprom.mk

extra_clean += $(wildcard ${BUILD_DIR}/*.h) $(wildcard */compile_commands.json)
extra_clean += ${EEPROM_FILE} librrprotocol.so librrprotocol.so.0
extra_clean += firmware.log
extra_clean_targets += gmatrix-clean
.PHONY: gmatrix-clean
gmatrix-clean:
	$(MAKE) -C gmatrix clean

BUILD_HEADERS += $(wildcard ${BUILD_DIR}/eeprom_layout.h)
BUILD_HEADERS += $(wildcard ${BUILD_DIR}/*.h)
ifeq ($(LIBRRPROTOCOL_PROVIDER),local)
BUILD_HEADERS += $(wildcard inc/librrprotocol/*.h)
endif
BUILD_HEADERS += $(wildcard inc/librustyaxe/*.h)

# Generated by ./tools/pack-eeprom.pl before any library/object is compiled;
# order-only prerequisites are used because the targets are produced by the
# same pack-eeprom invocation and their timestamps must not force relinking.
EEPROM_GENERATED := ${BUILD_DIR}/build_config.h ${BUILD_DIR}/eeprom_types.h ${BUILD_DIR}/eeprom_layout.h
OBJECT_ORDER_ONLY := | ${EEPROM_GENERATED}

# Directory-stamping prerequisites ensure the build tree exists before
# pack-eeprom-derived objects are written.
${BUILD_DIR}/librustyaxe/.timestamp ${BUILD_DIR}/librrprotocol/.stamp: ${BUILD_DIR}/.stamp

# Any build/ tree object (e.g. mongoose.c -> build/$PROFILE/librustyaxe/mongoose.o)
# must wait for pack-eeprom to generate the headers in build/$PROFILE first.
${BUILD_DIR}/%.o: | ${EEPROM_GENERATED}


extra_clean += librustyaxe.so librustyaxe.so.0 librustyirc.so librustyirc.so.0 librustyaxe/irc-test
ifeq ($(LIBRUSTYAXE_PROVIDER),local)
ifneq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
-include librustyaxe/rules.mk
else
include librustyaxe/rules.mk
endif
endif
ifeq ($(LIBRUSTYIRC_PROVIDER),local)
ifneq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
-include librustyaxe/librustyirc/rules.mk
else
include librustyaxe/librustyirc/rules.mk
endif
endif
ifeq ($(LIBRRPROTOCOL_PROVIDER),local)
ifneq ($(filter clean distclean uninstall,$(MAKECMDGOALS)),)
-include librrprotocol/rules.mk
else
include librrprotocol/rules.mk
endif
endif

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

# pack-eeprom-generated files (see mk/eeprom.mk) are built in one run.
${EEPROM_FILE}: $(wildcard res/*.json)

${BUILD_DIR}/.stamp:
	mkdir -p "${BUILD_DIR}"
	touch $@

world: after-eeprom

# The codec/GPSP harness also runs automatically in make test through
# rrclient/tests/test_codec_commands.sh (run-tests.sh discovers test_*.sh).
.PHONY: test-rrclient-codecs
test-rrclient-codecs: ${bins}
	bash rrclient/tests/test_codec_commands.sh

# Audit RustyRig-owned files for legacy dual-license boilerplate without
# modifying any source. Third-party submodules and Windows themes are excluded.
.PHONY: license-check license-migrate
license-check:
	perl tools/releng-gplv2-headers.pl --check

license-migrate:
	perl tools/releng-gplv2-headers.pl

# Build configured programs and run the maintained project test suites.
TEST_SUITES ?= selftest packaging librustyaxe librustyirc librrprotocol rrserver rrclient fwdsp libfwdspmgr www
.PHONY: test
test tests: ${bins} test-rrsdrutils
	./tests/run-tests.sh ${TEST_SUITES}

after-eeprom: ${EEPROM_FILE} ${BUILD_DIR}/build_config.h
after-eeprom: ${BUILD_DIR}/.stamp ${extra_build} ${bins}

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

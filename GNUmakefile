# New and improved GNU makefile
.PHONY: world
all: world
librustyaxe := librustyaxe.so
librrprotocol := librrprotocol.so

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
BUILD_DIR := ./build/${PROFILE}
include mk/compile.mk
include mk/database.mk
#include mk/libmongoose.mk
include mk/eeprom.mk

extra_clean += $(wildcard ${BUILD_DIR}/*.h) $(wildcard */compile_commands.json)
extra_clean += ${EEPROM_FILE} ${librustyaxe} ${librrprotocol}
extra_clean += firmware.log

BUILD_HEADERS += $(wildcard ${BUILD_DIR}/eeprom_layout.h)
BUILD_HEADERS += $(wildcard ${BUILD_DIR}/*.h)
BUILD_HEADERS += $(wildcard inc/librrprotocol/*.h)
BUILD_HEADERS += $(wildcard inc/librustyaxe/*.h)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

extra_clean += ${librustyaxe} librustyaxe/irc-test
include librustyaxe/rules.mk
include librrprotocol/rules.mk
include libfwdspmgr/rules.mk

ifeq (${BUILD_RRCLIENT},true)
include rrclient/rules.mk
endif

ifeq (${BUILD_RRSERVER},true)
include rrserver/rules.mk
endif

ifeq (${BUILD_FWDSP},true)
include fwdsp/rules.mk
endif

include mk/install.mk
include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk
include mk/debug.mk
include mk/resource.mk
include mk/packaging.mk

# This is built as part of ./tools/pack-eeprom until we split it off later perhaps
${BUILD_DIR}/build_config.h: ${EEPROM_FILE} .version

${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${BUILD_DIR}/.stamp:
	mkdir -p "${BUILD_DIR}"
	touch $@

world: after-eeprom

# Build configured programs and run the maintained project test suites.
# The external librustyaxe submodule suite can be selected explicitly.
TEST_SUITES ?= fwdsp librrprotocol rrclient rrserver selftest www librustyaxe
.PHONY: test
test tests: ${bins}
	./tests/run-tests.sh ${TEST_SUITES}

after-eeprom: ${EEPROM_FILE}
after-eeprom: ${BUILD_DIR}/.stamp ${BUILD_DIR}/build_config.h ${extra_build} ${bins}

audit-log:
	./tools/get-audit-log.sh

chat-log:
	./tools/get-chat-log.sh

ptt-log:
	./tools/get-ptt-log.sh

# Native programs and the child share the codec list and fallback pipelines.
${BUILD_DIR}/rrclient/defconfig.o ${BUILD_DIR}/rrserver/defconfig.o ${BUILD_DIR}/fwdsp/defconfig.o ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.o: fwdsp/default-pipelines.h

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
RRSERVER_HEADERS += $(wildcard rrserver/*.h)
RRCLIENT_HEADERS += $(wildcard rrclient/*.h)

rrclient_src = $(rrclient_objs:.o=.c)
rrserver_src = $(rrserver_objs:.o=.c)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

extra_clean += ${librustyaxe} librustyaxe/irc-test
include librustyaxe/rules.mk
include librrprotocol/rules.mk
include rrserver/rules.mk
include rrclient/rules.mk
#include fwdsp/rules.mk
include mk/install.mk
include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk
include mk/debug.mk
include mk/resource.mk
include mk/packaging.mk

# This is built as part of ./tools/pack-eeprom until we split it off later perhaps
${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${BUILD_DIR}/.stamp:
	mkdir -p "${BUILD_DIR}"
	touch $@

world: after-eeprom

after-eeprom: ${EEPROM_FILE}
after-eeprom: ${BUILD_DIR}/.stamp ${BUILD_DIR}/build_config.h ${extra_build} ${bins}

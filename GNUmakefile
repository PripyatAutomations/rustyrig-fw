# New and improved GNU makefile
all: world
BUILD_DIR := ./build
librustyaxe := librustyaxe.so
librrprotocol := librrprotocol.so

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
include mk/database.mk
#include mk/libmongoose.mk
include mk/eeprom.mk

extra_clean += $(wildcard ${OBJ_DIR}/*.h) $(wildcard */compile_commands.json)
extra_clean += ${EEPROM_FILE} ${librustyaxe} ${librrprotocol}
extra_clean += firmware.log

BUILD_HEADERS += $(wildcard ${OBJ_DIR}/eeprom_layout.h)
BUILD_HEADERS += $(wildcard ${OBJ_DIR}/*.h)
BUILD_HEADERS += $(wildcard rrserver/*.h) $(wildcard rrclient/*.h)
BUILD_HEADERS += $(wildcard librrprotocol/*.h)
BUILD_HEADERS += $(wildcard librustyaxe/*.h)

rrclient_src = $(rrclent_objs:.o=.c)
rrserver_src = $(rrserver_objs:.o=.c)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

extra_clean += ${librustyaxe} librustyaxe/irc-test
include librustyaxe/rules.mk
include rrcli/rules.mk
include librrprotocol/rules.mk
include rrclient/rules.mk
include rrserver/rules.mk
include mk/install.mk
#include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk
include mk/debug.mk
include mk/resource.mk
include mk/compile.mk

${OBJ_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${OBJ_DIR}/.stamp:
	mkdir -p ${OBJ_DIR}
	touch $@

world: ${OBJ_DIR}/.stamp ${extra_build} ${bins}

irc-test: ${librustyaxe}
	${MAKE} -C librustyaxe ../bin/irc-test

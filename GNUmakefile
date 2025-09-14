# New and improved GNU makefile
all: world

librustyaxe := librustyaxe.so

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
include mk/database.mk
include mk/libmongoose.mk
include mk/eeprom.mk

extra_clean += $(wildcard ${OBJ_DIR}/*.h) $(wildcard */compile_commands.json)
extra_clean += ${EEPROM_FILE} librustyaxe.so
extra_clean += firmware.log
extra_clean_targets += clean-librustyaxe

#BUILD_HEADERS += ${OBJ_DIR}/eeprom_layout.h
BUILD_HEADERS += $(wildcard inc/rrserver/*.h) $(wildcard inc/rrclient/*.h)
BUILD_HEADERS += $(wildcard inc/librustyaxe/*.h) $(wildcard ${OBJ_DIR}/*.h)

bins := bin/rrclient bin/rrserver bin/fwdsp
fwdsp_src = $(fwdsp_objs:.o=.c)
rrclient_src = $(rrclent_objs:.o=.c)
rrserver_src = $(rrserver_objs:.o=.c)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

librustyaxe_src = $(wildcard librustyaxe/*.c) $(wildcard librustyaxe/*.h)

${librustyaxe}: ${librustyaxe_src}
	${MAKE} -C librustyaxe -j4 world

clean-librustyaxe:
	${MAKE} -C librustyaxe distclean

include mk/resource.mk
include mk/debug.mk
include fwdsp/rules.mk
include rrclient/rules.mk
include rrserver/rules.mk
include mk/compile.mk
include mk/install.mk
include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk

${OBJ_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${OBJ_DIR}/.stamp:
	mkdir -p ${OBJ_DIR}
	touch $@

world: ${OBJ_DIR}/.stamp ${extra_build} ${bins}

all: world
VERSION=$(shell cat ../../.version)
PROFILE ?= radio
CF := ../../config/${PROFILE}.config.json
CHANNELS := ../../config/${PROFILE}.channels.json
BUILD_DIR := ../../build/${PROFILE}
OBJ_DIR := ${BUILD_DIR}/obj
INSTALL_DIR = /usr/local
CONF_DIR := /etc/rustyrig

fw_bin := ${BUILD_DIR}/rrserver
fwdsp_bin := ${BUILD_DIR}/fwdsp

bins += ${fw_bin} ${fwdsp_bin}

ifeq (x$(wildcard ${CF}),x)
$(error ***ERROR*** Please create ${CF} first before building -- There is an example at doc/radio.json.example you can use)
endif

CFLAGS_WARN += $(strip $(shell cat ${CF} | jq -r ".build.cflags_warn")) -I../../inc/
CFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.cflags")) -I../../inc/
CFLAGS += -DMG_ENABLE_IPV6=1
LDFLAGS := -lc -lm -g -ggdb -lcrypt

CFLAGS += -I${BUILD_DIR} -I${BUILD_DIR}/include 
CFLAGS += -DLOGFILE="\"$(strip $(shell cat ${CF} | jq -r '.debug.logfile'))\""
CFLAGS += -DCONFDIR="\"${CONF_DIR}\"" -DVERSION="\"${VERSION}\""
#CFLAGS += -DUSE_EEPROM

ifneq (x${DEBUG_PROTO},x)
CFLAGS += -DDEBUG_PROTO
endif

# Parse the config/${PROFILE}.config.json file
include ../../mk/json-config.mk

FWDSP_CFLAGS += -D__FWDSP

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

ifeq (${USE_OPUS},true)
LDFLAGS += $(shell pkg-config --libs opus)
CFLAGS += $(shell pkg-config --cflags opus)
endif

ifeq (${USE_SSL},true)
CFLAGS += -DMG_TLS=MG_TLS_MBED
LDFLAGS += -lmbedcrypto -lmbedtls -lmbedx509
endif

ifeq (${USE_GSTREAMER},true)
FWDSP_CFLAGS += $(shell pkg-config --cflags gstreamer-1.0)
FWDSP_LDFLAGS += $(shell pkg-config --libs gstreamer-1.0)
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

ifeq (${PLATFORM},posix)
comm_objs += posix.o			# support for POSIX hosts (linux or perhaps others)
LDFLAGS += -lgpiod
endif

include ../../mk/common.objs.mk

fwdsp_objs += fwdsp-main.o

# translate unprefixed object file names to source file names
fw_src = $(fw_objs:.o=.c)
fwdsp_src = $(fwdsp_objs:.o:.c)

# prepend objdir path to each object
real_comm_objs := $(foreach x, ${comm_objs}, ${OBJ_DIR}/common/${x})
real_fw_objs := $(foreach x, ${fw_objs}, ${OBJ_DIR}/firmware/${x})
real_fwdsp_objs := $(foreach x, ${fwdsp_objs}, ${OBJ_DIR}/fwdsp/${x})
real_fw_objs += ${OBJ_DIR}/firmware/logger.o
real_fw_objs += ${OBJ_DIR}/firmware/config.o
real_fwdsp_objs += ${OBJ_DIR}/fwdsp/logger.o
real_fwdsp_objs += ${OBJ_DIR}/fwdsp/config.o

################################################################################
###############
# Build Rules #
###############
extra_clean += firmware.log
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
#extra_build += ${EEPROM_FILE}
#extra_clean += ${EEPROM_FILE}

world: ${extra_build} ${bins}

#BUILD_HEADERS=${BUILD_DIR}/build_config.h
#BUILD_HEADERS += ${BUILD_DIR}/eeprom_layout.h
BUILD_HEADERS += $(wildcard ../../inc/rustrig/*.h) $(wildcard ${BUILD_DIR}/*.h)

${OBJ_DIR}/firmware/%.o: %.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/common/%.o: ../common/%.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] shared $@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

# fwdsp
${OBJ_DIR}/fwdsp/%.o: ../fwdsp/%.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<


${OBJ_DIR}/fwdsp/config.o: ../common/config.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] fwdsp:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/firmware/config.o: ../common/config.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] server:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/fwdsp/logger.o: ../common/logger.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] fwdsp:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/firmware/logger.o: ../common/logger.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] server:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/common/mongoose.o: ../../ext/libmongoose/mongoose.c ${BUILD_HEADERS}
# delete the old object file, so we can't accidentally link against it if compile failed...
	@${RM} -f $@
	@echo "[compile] mongoose from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<

${fw_bin}: ${real_fw_objs} ${real_comm_objs}
	@echo "[Link] firmware ($@) from $(words ${real_fw_objs}) object files..."
	@${CC} -o $@ ${real_fw_objs} ${real_comm_objs} ${LDFLAGS}
	@ls -a1ls $@
	@file $@
	@size $@

${fwdsp_bin}: ${real_fwdsp_objs} ${real_comm_objs}
	@echo "[Link] fwdsp ($@) from $(words ${real_fwdsp_objs}) object files..."
	@${CC} -o $@ ${real_fwdsp_objs} ${real_comm_objs} ${LDFLAGS} ${FWDSP_LDFLAGS}
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${fw_bin} ${fwdsp_bin}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

pack-eeprom: ../../tools/pack-eeprom.pl ${EEPROM_FILE}
	@echo "[pack-eeprom]" 
	set -e; ../../tools/pack-eeprom.pl ${PROFILE}

##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bins} ${real_fwdsp_objs} ${real_fw_objs} ${extra_clean} ${real_comm_objs}

distclean: clean
	@echo "[distclean]"
	${RM} -r build
	${RM} -f ../../config/archive/*.json *.log ../../state/*

install:
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	cp -av ${bins}  ${INSTALL_DIR}/bin
#	cp -av archive-config.sh *-rigctld.sh fwdsp-test.sh killall.sh rrclient.sh test-run.sh ${INSTALL_DIR}/bin
#	cp -aiv config/${PROFILE}.*.json config/client.config.json ${INSTALL_DIR}/etc
 
###################
# Running on host #
###################
ifeq (${PLATFORM},posix)
run: ${MASTER_DB} ${EEPROM_FILE} ${fw_bin}
#	${fw_bin}
	./test-run.sh

gdb debug: ${fw_bin} ${EEPROM_FILE}
	@echo "[gdb] ${fw_bin}"
	@gdb ${fw_bin} -ex 'run'
	@./killall.sh

test: clean world run

endif

#################
# Configuration #
#################
# Display an error message and halt the build, if no configuration file
${CF}:
	@echo "********************************************************"
	@echo "* PLEASE read README.txt and edit ${CF} as needed *"
	@echo "********************************************************"
	exit 1

installdep:
	@./install-deps.sh

config/http.users:
	@echo "*** You do not have a config/http.users so i've copied the default file from doc/"
	cp -i doc/http.users.example config/http.users

symtab: ${fwdsp_bin}.symtab

${fwdsp_bin}.symtab: ${fwdsp_bin}
	nm ${fwdsp_bin} |grep -v '.gtk_*' | grep -v '.mg_*'|grep -v '.[Ur].' > $@

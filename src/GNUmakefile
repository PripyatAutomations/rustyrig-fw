ifeq (x$(wildcard radio.json),x)
$(error ERROR!!! Please create radio.json first)
endif

CONFIG ?= radio
CF := ${CONFIG}.json
CFLAGS := -std=gnu11 -g -O2 -Wall -Wno-unused -pedantic
LDFLAGS := -lc -lm
BUILD_DIR := build/${CONFIG}

# Pull target configuration bits out of the config .json file
CFLAGS += -I${BUILD_DIR} $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
LDFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.ldflags"))
TC_PREFIX := $(strip $(shell cat ${CF} | jq -r ".build.toolchain.prefix"))
EEPROM_SIZE := $(shell cat ${CF} | jq -r ".eeprom.size")
EEPROM_FILE := ${BUILD_DIR}/eeprom.bin
PLATFORM := $(strip $(shell cat ${CF} | jq -r ".build.platform"))

ifneq (${TC_PREFIX},"")
CC := ${TC_PREFIX}-gcc
LD := ${TC_PREFIX}-ld
else
CC := gcc
LD := ld
endif
OBJ_DIR := ${BUILD_DIR}/obj

bin := ${BUILD_DIR}/firmware.bin

objs += cat_control.o		# CAT parsers
objs += eeprom.o		# "EEPROM" configuration storage
objs += faults.o		# Fault management/alerting
objs += gpio.o			# GPIO controls
objs += i2c.o			# i2c abstraction
objs += logger.o		# Logging facilities
objs += main.o			# main loop
ifeq (${PLATFORM}, posix)
objs += posix.o			# support for POSIX hosts (pi or debugging)
endif
objs += power.o			# Power monitoring and management
objs += thermal.o		# Thermal management
objs += usb.o			# Support for USB control (stm32)

################################################################################
real_objs := $(foreach x, ${objs}, ${OBJ_DIR}/${x})
extra_clean += firmware.log firmware.bin eeprom.bin ${EEPROM_FILE}
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_build += ${EEPROM_FILE}
src_files = $(wildcard *.c)

all: world

world: ${bin} ${extra_build}

${bin}: radio.json ${real_objs} .version
	${CC} ${LDFLAGS} -o $@ ${real_objs}

${OBJ_DIR}/%.o:%.c $(wildcard *.h) $(wildcard ${BUILD_DIR}/*.h) .version GNUmakefile ${CF}
	${CC} ${CFLAGS} -o $@ -c $<

${src_files}: ${BUILD_DIR}/build_config.h ${BUILD_DIR}/eeprom_layout.h 

clean:
	${RM} ${bin} ${real_objs} ${extra_clean}

${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${BUILD_DIR}/eeprom_layout.h: ${EEPROM_FILE}

${EEPROM_FILE}: $(wildcard *.h) buildconf.pl
	./buildconf.pl ${CONFIG}

run: ${bin} ${EEPROM_FILE}
	${bin}

# Run debugger
gdb: ${bin} ${EEPROM_FILE}
	gdb ${bin} -ex 'run'

test: clean world gdb

distclean: clean
	${RM} ${EEPROM_FILE}
	${RM} -r build/*

install:
	@echo "Automatic DFU installation isn't supported yet... Please see doc/INSTALLING.txt for more info"

radio.json:
	@echo "********************************************************"
	@echo "* PLEASE read README.txt and edit radio.json as needed *"
	@echo "********************************************************"
	exit 1

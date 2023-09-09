#
# Build the firmware
#

CONFIG_FILE=radio.json

# Are we building for the host (posix) or a target device?

# Usually, say n here, unless developing
HOST_BUILD=y

ifeq (${HOST_BUILD},n)
TARGET_PREFIX := arm-none-eabi
BUILD_DIR := build/radio
OBJ_PREFIX := ${BUILD_DIR}/obj
else
BUILD_DIR := build/host
OBJ_PREFIX := ${BUILD_DIR}/obj
endif

CFLAGS := -std=gnu11 -fPIC -g -O2 -I${BUILD_DIR}
LDFLAGS := -lc -lm

ifneq (x${TARGET_PREFIX},x)
CC := ${TARGET_PREFIX}-gcc
LD := ${TARGET_PREFIX}-ld
CFLAGS +=
LDFLAGS +=
else
CFLAGS += -DHOST_BUILD=1
CC := gcc
LD := ld
endif

# XXX: get this from radio.json
EEPROM_SIZE := 16384
EEPROM_FILE := ${BUILD_DIR}/eeprom.bin

bin := ${BUILD_DIR}/firmware.bin

objs += eeprom.o		# "EEPROM" configuration storage
objs += faults.o		# Fault management/alerting
objs += gpio.o			# GPIO controls
objs += i2c.o			# i2c abstraction
objs += logger.o		# Logging facilities
objs += main.o			# main loop
objs += parser.o		# CAT parsers
objs += posix.o			# support for POSIX hosts (pi or debugging)
objs += power.o			# Power monitoring and management
objs += thermal.o		# Thermal management
objs += usb.o			# Support for USB control (stm32)

real_objs := $(foreach x, ${objs}, ${OBJ_PREFIX}/${x})
extra_clean += firmware.log firmware.bin eeprom.bin ${EEPROM_FILE}
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_build += ${EEPROM_FILE}
src_files = $(wildcard *.c)

all: world

world: ${bin} ${extra_build}

${bin}: ${real_objs}
	${CC} ${LDFLAGS} -o $@ ${real_objs}

${OBJ_PREFIX}/%.o:%.c $(wildcard *.h) $(wildcard ${BUILD_DIR}/*.h)
	${CC} ${CFLAGS} -o $@ -c $<

${src_files}: ${BUILD_DIR}/build_config.h ${BUILD_DIR}/eeprom_layout.h 

clean:
	${RM} ${bin} ${real_objs} ${extra_clean}


${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${BUILD_DIR}/eeprom_layout.h: ${EEPROM_FILE}

${EEPROM_FILE}: $(wildcard *.h) mkeeprom.pl
	./mkeeprom.pl ${EEPROM_FILE} ${CONFIG_FILE}

run: ${bin} ${EEPROM_FILE}
	${bin}

# Run debugger
gdb: ${bin} ${EEPROM_FILE}
ifneq (x${HOST_BUILD},x)
	gdb ${bin} -ex 'run'
endif

test: clean world gdb

distclean: clean
	${RM} ${EEPROM_FILE}
	${RM} $(foreach x, ${objs}, build/host/obj/${x})
	${RM} $(foreach x, ${objs}, build/radio/obj/${x})

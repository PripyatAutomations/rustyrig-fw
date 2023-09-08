#
# Build the firmware
HOST_BUILD=y

ifeq (${HOST_BUILD},n)
TARGET_PREFIX := arm-none-eabi
BUILD_DIR := build/host/
OBJ_PREFIX := ${BUILD_DIR}/obj
else
BUILD_DIR := build/radio/
OBJ_PREFIX := ${BUILD_DIR}/obj
endif
CFLAGS := -std=gnu11 -fPIC -g -O2
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

# XXX: Fix this
#EEPROM_SIZE := $(shell grep EEPROM_SIZE config.h|awk '{print $3}')
#EEPROM_FILE := $(shell grep HOST_EEPROM_FILE config.h|awk '{print $3}')
EEPROM_SIZE := 16384
EEPROM_FILE := eeprom.bin

bin := firmware.bin

objs += eeprom.o
objs += faults.o
objs += i2c.o
objs += logger.o
objs += main.o
objs += parser.o
objs += power.o
objs += thermal.o

real_objs := $(foreach x, ${objs}, ${OBJ_PREFIX}/${x})
extra_clean += firmware.log
extra_build += eeprom.bin

all: world

world: ${bin} ${extra_build}

${bin}: ${real_objs}
	${CC} ${LDFLAGS} -o $@ $^

${OBJ_PREFIX}/%.o:%.c $(wildcard *.h)
	${CC} ${CFLAGS} -o $@ -c $<

clean:
	${RM} ${bin} ${real_objs} ${extra_clean}

eeprom: ${EEPROM_FILE}

${EEPROM_FILE}:
	dd if=/dev/zero of=${EEPROM_FILE} bs=${EEPROM_SIZE} count=1
	# XXX: Apply the default values at DefaultValues offset

# Run debugger
gdb:
ifneq (x${HOST_BUILD},x)
	gdb ${bin} -ex 'run'
endif

test: clean world gdb

distclean: clean
	${RM} ${EEPROM_FILE}
	${RM} $(foreach x, ${objs}, build/host/obj/${x})
	${RM} $(foreach x, ${objs}, build/radio/obj/${x})

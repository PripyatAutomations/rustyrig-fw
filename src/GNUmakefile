#
# Build the firmware

#TARGET_PREFIX := arm-none-eabi
CFLAGS := -std=gnu11 -fPIC
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

bin := firmware.bin

objs += eeprom.o
objs += logger.o
objs += main.o
objs += parser.o
objs += power.o
objs += thermal.o

real_objs := $(foreach x, ${objs}, .obj/${x})
extra_clean += firmware.log
all: world

world: ${bin}

${bin}: ${real_objs}
	${CC} ${LDFLAGS} -o $@ $^

.obj/%.o:%.c $(wildcard *.h)
	${CC} ${CFLAGS} -o $@ -c $<

clean:
	${RM} ${bin} ${real_objs} ${extra_clean}


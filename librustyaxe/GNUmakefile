all: world

CFLAGS := -Wall -O2 -fPIC -ggdb -g -I../ -I${BUILD_DIR}/ -Wno-unused
LDFLAGS := -shared
CFLAGS += $(shell pkg-config --cflags mbedtls)
LDFLAGS += $(shell pkg-config --libs mbedtls mbedcrypto mbedx509)

librustyaxe := ../librustyaxe.so
libs := ${librustyaxe}

# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librr_objs += /g'
librr_objs += audio.framing.o
librr_objs += cat.o
librr_objs += cat.kpa500.o
librr_objs += cat.yaesu.o
librr_objs += codecneg.o
librr_objs += config.o
librr_objs += daemon.o
librr_objs += dict.o
librr_objs += io.o
librr_objs += io.serial.o
librr_objs += io.socket.o
librr_objs += irc.parser.o
librr_objs += json.o
librr_objs += logger.o
librr_objs += maidenhead.o
librr_objs += module.o
librr_objs += posix.o
librr_objs += ringbuffer.o
# XXX: This needs cleanup to remove remnants of termbox/old logger
#librr_objs += subproc.o
librr_objs += util.file.o
librr_objs += util.math.o
librr_objs += util.string.o
librr_objs += util.time.o
librr_objs += ws.mediachan.o
librr_srcs := $(librr_objs:.o=.c)
headers := $(wildcard *.h)
real_librr_objs := $(foreach x, ${librr_objs}, obj/${x})

${librustyaxe}: ${real_librr_objs} GNUmakefile
	@echo "[link] $@ from $(words $^) objects"
	@${CC} ${LDFLAGS} -o $@ ${real_librr_objs}

world: ${libs}

${librr_srcs}: GNUmakefile ${headers}

obj/%.o:%.c GNUmakefile ${headers}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${CFLAGS} -o $@ -c $<

clean:
	@echo "[clean]"
	${RM} -f ${real_librr_objs} librustyaxe.so

distclean: clean
	@echo "[distclean]"

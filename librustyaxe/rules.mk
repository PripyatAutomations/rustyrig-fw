
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
librr_headers := $(wildcard *.h)

ifeq (x${OBJ_DIR}, x)
real_librr_objs := $(foreach x, ${librr_objs}, obj/${x})
else
real_librr_objs := $(foreach x, ${librr_objs}, ${OBJ_DIR}/${x})
endif

${librr_srcs}: GNUmakefile ${librr_headers} rules.mk

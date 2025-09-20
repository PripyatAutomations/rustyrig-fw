librustyaxe ?= librustyaxe.so
# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librustyaxe_objs += /g'
#librustyaxe_objs += audio.framing.o
librustyaxe_objs += cat.o
librustyaxe_objs += cat.kpa500.o
librustyaxe_objs += cat.yaesu.o
librustyaxe_objs += codecneg.o
librustyaxe_objs += config.o
librustyaxe_objs += daemon.o
librustyaxe_objs += dict.o
librustyaxe_objs += io.o
librustyaxe_objs += io.serial.o
librustyaxe_objs += io.socket.o
librustyaxe_objs += irc.capab.o
librustyaxe_objs += irc.commands.o
librustyaxe_objs += irc.modes.o
librustyaxe_objs += irc.numerics.o
librustyaxe_objs += irc.parser.o
librustyaxe_objs += json.o
librustyaxe_objs += logger.o
librustyaxe_objs += maidenhead.o
librustyaxe_objs += module.o
librustyaxe_objs += posix.o
librustyaxe_objs += ringbuffer.o
# XXX: This needs cleanup to remove remnants of termbox/old logger
#librustyaxe_objs += subproc.o
librustyaxe_objs += util.file.o
librustyaxe_objs += util.math.o
librustyaxe_objs += util.string.o
librustyaxe_objs += util.time.o
extra_clean += ${librustyaxe_objs} ${librustyaxe}

librustyaxe_headers := $(wildcard librustyaxe/*.h)
librustyaxe_src = $(wildcard librustyaxe/*.c) $(wildcard librustyaxe/*.h)

real_librustyaxe_objs := $(foreach x, ${librustyaxe_objs}, ${BUILD_DIR}/librustyaxe/${x})

libs += ${librustyaxe}

${librustyaxe_srcs}: GNUmakefile ${librustyaxe_headers} librustyaxe/rules.mk

librustyaxe-pre:
	mkdir -p ${BUILD_DIR}/librustyaxe

${librustyaxe}: librustyaxe-pre ${real_librustyaxe_objs} ${librustyaxe_headers} GNUmakefile librustyaxe/rules.mk
	@echo "[link] $@ from $(words ${real_librustyaxe_objs}) objects"
	@${CC} ${LDFLAGS} -lm -fPIC -shared -o $@ ${real_librustyaxe_objs}

${BUILD_DIR}/librustyaxe/%.o:librustyaxe/%.c GNUmakefile ${librustyaxe_headers}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${CFLAGS} -o $@ -c $<

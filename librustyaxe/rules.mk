CFLAGS +=$(call pkgconfig, --cflags tinfo)
LDFLAGS +=$(call pkgconfig, --libs tinfo)

librustyaxe ?= ../librustyaxe.so
# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librustyaxe_objs += /g'
#librustyaxe_objs += cat.o
#librustyaxe_objs += cat.kpa500.o
#librustyaxe_objs += cat.yaesu.o
librustyaxe_objs += config.o
#librustyaxe_objs += daemon.o
librustyaxe_objs += dict.o
librustyaxe_objs += driver-csi.o
librustyaxe_objs += driver-ti.o
#librustyaxe_objs += io.o
#librustyaxe_objs += io.serial.o
#librustyaxe_objs += io.socket.o
librustyaxe_objs += irc.o
librustyaxe_objs += irc.capab.o
librustyaxe_objs += irc.client.o
librustyaxe_objs += irc.commands.o
librustyaxe_objs += irc.modes.o
librustyaxe_objs += irc.numerics.o
librustyaxe_objs += irc.parser.o
librustyaxe_objs += irc.server.o
librustyaxe_objs += json.o
librustyaxe_objs += list.o
librustyaxe_objs += logger.o
librustyaxe_objs += maidenhead.o
librustyaxe_objs += module.o
librustyaxe_objs += posix.o
librustyaxe_objs += ringbuffer.o
# XXX: This needs cleanup to remove remnants of termbox/old logger
#librustyaxe_objs += subproc.o
librustyaxe_objs += termkey.o
librustyaxe_objs += tui.o
librustyaxe_objs += tui.keys.o
librustyaxe_objs += tui.theme.o
librustyaxe_objs += tui.window.o
librustyaxe_objs += tui.completion.o
librustyaxe_objs += util.file.o
librustyaxe_objs += util.math.o
librustyaxe_objs += util.string.o
librustyaxe_objs += util.time.o

librustyaxe_headers := $(wildcard librustyaxe/*.h)
librustyaxe_src = $(wildcard librustyaxe/*.c) $(wildcard librustyaxe/*.h)

real_librustyaxe_objs := $(foreach x, ${librustyaxe_objs}, ${BUILD_DIR}/librustyaxe/${x})
extra_clean += ${real_librustyaxe_objs} ${librustyaxe}

libs += ${librustyaxe}

${librustyaxe_srcs}: GNUmakefile ${librustyaxe_headers}

librustyaxe-pre:
	mkdir -p ${BUILD_DIR}/librustyaxe

${librustyaxe}: librustyaxe-pre ${real_librustyaxe_objs} ${librustyaxe_headers} GNUmakefile
	@${RM} -f $@
	@echo "[link] $@ from $(words ${real_librustyaxe_objs}) objects"
	@${CC} -fPIC -shared -o $@ ${real_librustyaxe_objs}  -lm -lev -ltinfo ${LDFLAGS}|| exit 2

${BUILD_DIR}/librustyaxe/%.o:librustyaxe/%.c GNUmakefile ${librustyaxe_headers}
	@${RM} $@
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} -o $@ -c $< || exit 2

bin/irc-test: ${BUILD_DIR}/irc-test.o ${librustyaxe}
	@${RM} $@
	$(CC) -L. -o $@ $< -lrustyaxe -lm -lev $(LDFLAGS) 

${BUILD_DIR}/irc-test.o: librustyaxe/irc-test.c $(wildcard *.h)
	@${RM} $@
	@echo "[compile] $< => $@"
	@$(CC) $(CFLAGS) -I. -I.. -o $@ -c $<

extra_clean += irc-test ${BUILD_DIR}/irc-test.o

bins += bin/irc-test

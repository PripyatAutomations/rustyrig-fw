librrprotocol ?= librrprotocol.so
libs += ${librrprotocol}

# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librrprotocol_objs += /g'
librrprotocol_objs += auth.o
librrprotocol_objs += backend.o
librrprotocol_objs += codecneg.o
librrprotocol_objs += http.o
librrprotocol_objs += http.api.o
librrprotocol_objs += http.bans.o
librrprotocol_objs += vfo.o
librrprotocol_objs += ws.o
librrprotocol_objs += ws.alert.o
librrprotocol_objs += ws.audio.o
librrprotocol_objs += ws.auth.o
librrprotocol_objs += ws.bcast.o
librrprotocol_objs += ws.chat.o
librrprotocol_objs += ws.chat.cli.o
librrprotocol_objs += ws.cli.o
librrprotocol_objs += ws.error.o
librrprotocol_objs += ws.file-xfer.o
librrprotocol_objs += ws.media.o
librrprotocol_objs += ws.mediachan.o
librrprotocol_objs += ws.notice.o
librrprotocol_objs += ws.ping.o
librrprotocol_objs += ws.rigctl.o
librrprotocol_objs += ws.rigctl.cli.o
librrprotocol_objs += ws.syslog.o

extra_clean += ${librustyaxe_objs} ${librustyaxe}
librrprotocol_headers := $(wildcard librrprotocol/*.h)
librrprotocol_src = $(wildcard librrprotocol/*.c) $(wildcard librrprotocol/*.h)

real_librrprotocol_objs := $(foreach x, ${librrprotocol_objs}, ${BUILD_DIR}/librrprotocol/${x})
${librrprotocol_srcs}: GNUmakefile ${librrprotocol_headers} librrprotocol/rules.mk

librrprotocol-pre:
	mkdir -p ${BUILD_DIR}/librrprotocol/

${librrprotocol}: librrprotocol-pre ${real_librrprotocol_objs} ${librrprotocol_headers} GNUmakefile librrprotocol/rules.mk
	@echo "[link] $@ from $(words ${real_librrprotocol_objs}) objects"
	@${CC} ${LDFLAGS} -lm -fPIC -shared -o $@ ${real_librrprotocol_objs} || exit 2

${BUILD_DIR}/librrprotocol/%.o:librrprotocol/%.c GNUmakefile ${librrprotocol_headers}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${CFLAGS} -o $@ -c $< || exit 2

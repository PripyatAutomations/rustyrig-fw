# libfwdspmgr: the fwdsp manager library used by rrserver and rrclient.
# It has no gstreamer dependency; that lives in the fwdsp binary itself.

CFLAGS_LIBFWDSPMGR := ${CFLAGS} -I${BUILD_DIR}

libfwdspmgr_objs := fwdsp-mgr.o fwdsp-ctl.o fwdsp-video.o

# Public header consumers (rrserver/rrclient) include <libfwdspmgr/fwdsp-mgr.h>
BUILD_HEADERS += ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h

${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h: libfwdspmgr/fwdsp-mgr.h GNUmakefile
	@mkdir -p $(dir $@)
	@cp $< $@

libfwdspmgr := libfwdspmgr.so
libs += ${libfwdspmgr}

real_libfwdspmgr_objs := $(addprefix ${BUILD_DIR}/libfwdspmgr/,$(libfwdspmgr_objs))
extra_clean += ${real_libfwdspmgr_objs} ${libfwdspmgr} libfwdspmgr.so.0

${libfwdspmgr}: ${real_libfwdspmgr_objs} ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h ${librustyaxe} ${librrprotocol} GNUmakefile
	@${RM} -f $@
	@echo "[link] $@ from $(words ${real_libfwdspmgr_objs}) objects"
	@${CC} ${LIB_LDFLAGS} -Wl,-soname,libfwdspmgr.so.0 -o $@ ${real_libfwdspmgr_objs} -lrustyaxe -lrrprotocol ${LDFLAGS}
	@ln -sf libfwdspmgr.so libfwdspmgr.so.0
	@ls -a1ls $@

${BUILD_DIR}/libfwdspmgr/%.o: libfwdspmgr/%.c ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h GNUmakefile libfwdspmgr/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(dir $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_LIBFWDSPMGR} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

libfwdspmgr_srcs := $(wildcard libfwdspmgr/*.c)
${libfwdspmgr_srcs}: GNUmakefile ${librrprotocol_headers} librrprotocol/rules.mk ${BUILD_DIR}/build_config.h

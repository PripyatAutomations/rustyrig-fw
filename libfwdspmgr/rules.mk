# libfwdspmgr: the fwdsp manager library used by rrserver and rrclient.
# It has no gstreamer dependency; that lives in the fwdsp binary itself.

CFLAGS_LIBFWDSPMGR := ${CFLAGS} -I${BUILD_DIR}

libfwdspmgr_objs += fwdsp-mgr.o

# Public header consumers (rrserver/rrclient) include <libfwdspmgr/fwdsp-mgr.h>
BUILD_HEADERS += ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h

${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h: libfwdspmgr/fwdsp-mgr.h GNUmakefile
	@mkdir -p $(shell dirname $@)
	@cp $< $@

libfwdspmgr := libfwdspmgr.so
libs += ${libfwdspmgr}
real_libfwdspmgr_objs := $(foreach x, ${libfwdspmgr_objs}, ${BUILD_DIR}/libfwdspmgr/${x})
extra_clean += ${real_libfwdspmgr_objs} ${libfwdspmgr}

${libfwdspmgr}: ${real_libfwdspmgr_objs} ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h ${librustyaxe} ${librrprotocol} GNUmakefile
	@${RM} -f $@
	@echo "[link] $@ from $(words ${real_libfwdspmgr_objs}) objects"
	@${CC} ${LIB_LDFLAGS} -o $@ ${real_libfwdspmgr_objs} -lrustyaxe -lrrprotocol ${LDFLAGS} || exit 2
	@ls -a1ls $@

${BUILD_DIR}/libfwdspmgr/%.o: libfwdspmgr/%.c ${BUILD_DIR}/libfwdspmgr/fwdsp-mgr.h GNUmakefile libfwdspmgr/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_LIBFWDSPMGR} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

# Here we build fwdsp, which provides access to gstreamer pipelines
# from rrserver or rrclient
# - Eventually on embedded targets this will be replaced with
# a codec chip that supports a few common codecs.

CFLAGS_FWDSP := ${CFLAGS} -I${BUILD_DIR} #-DLOGFILE="\"fwdsp.log\"" -I${BUILD_DIR}/libfwdspmgr
LDFLAGS_FWDSP := ${LDFLAGS} -L. -lrustyaxe ${gst_ldflags}
fwdsp := bin/fwdsp
bins += ${fwdsp}

libfwdspmgr_objs += fwdsp-mgr.o
fwdsp_objs += defconfig.o
fwdsp_objs += fwdsp.o

# Public headers others (rrserver/rrclient) include
BUILD_HEADERS += ${BUILD_DIR}/rrserver/fwdsp-mgr.h

# Copy the header into the build include path so <rrserver/fwdsp-mgr.h> works
${BUILD_DIR}/rrserver/fwdsp-mgr.h: fwdsp/fwdsp-mgr.h GNUmakefile
	@mkdir -p $(shell dirname $@)
	@cp $< $@

libfwdspmgr := libfwdspmgr.so
libs += ${libfwdspmgr}
real_libfwdspmgr_objs := $(foreach x, ${libfwdspmgr_objs}, ${BUILD_DIR}/fwdsp/${x})
extra_clean += ${real_libfwdspmgr_objs} ${libfwdspmgr}

${libfwdspmgr}: ${real_libfwdspmgr_objs} ${BUILD_DIR}/rrserver/fwdsp-mgr.h GNUmakefile
	@${RM} -f $@
	@echo "[link] $@ from $(words ${real_libfwdspmgr_objs}) objects"
	@${CC} ${LIB_LDFLAGS} -o $@ ${real_libfwdspmgr_objs} -lrustyaxe -lrrprotocol ${LDFLAGS} ${gst_ldflags} || exit 2
	@ls -a1ls $@

fwdsp_real_objs := $(foreach x, ${fwdsp_objs}, ${BUILD_DIR}/fwdsp/${x})

${BUILD_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS} GNUmakefile fwdsp/rules.mk ${librustyaxe_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${fwdsp_real_objs}
	@echo "[link] $< => $@"
	@${CC}  -o $@ ${fwdsp_real_objs} -lrustyaxe ${LDFLAGS} ${LDFLAGS_FWDSP} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

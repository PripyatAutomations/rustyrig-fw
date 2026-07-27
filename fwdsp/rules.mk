# Here we build fwdsp, which provides access to gstreamer pipelines
# from rrserver or rrclient
# - Eventually on embedded targets this will be replaced with
# a codec chip that supports a few common codecs.

CFLAGS_FWDSP := ${CFLAGS} #-DLOGFILE="\"fwdsp.log\""
LDFLAGS_FWDSP := ${LDFLAGS} -L. -lrustyaxe ${gst_ldflags}
fwdsp := bin/fwdsp
bins += ${fwdsp}
libs += libfwdspmgr

libfwdspmgr_objs += fwdsp-mgr.o
fwdsp_objs += defconfig.o
fwdsp_objs += fwdsp.o

fwdsp_real_objs := $(foreach x, ${fwdsp_objs}, ${OBJ_DIR}/fwdsp/${x})

${OBJ_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS} GNUmakefile fwdsp/rules.mk ${librustyaxe_headers}
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

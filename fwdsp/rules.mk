# Here we build fwdsp, which provides access to gstreamer pipelines
# from rrserver or rrclient
# - Eventually on embedded targets this will be replaced with
# a codec chip that supports a few common codecs.

# The fwdsp manager library lives in libfwdspmgr/ (see libfwdspmgr/rules.mk);
# rrserver and rrclient link against it, fwdsp itself does not.

CFLAGS_FWDSP := ${CFLAGS} -I${BUILD_DIR}
LDFLAGS_FWDSP := ${LDFLAGS} -L. -lrustyaxe ${gst_ldflags} -lFLAC -lpthread
fwdsp := bin/fwdsp
bins += ${fwdsp}

fwdsp_objs += defconfig.o
fwdsp_objs += fwdsp.o

fwdsp_real_objs := $(foreach x, ${fwdsp_objs}, ${BUILD_DIR}/fwdsp/${x})

${BUILD_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS} GNUmakefile fwdsp/rules.mk ${librustyaxe_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ./libfwdspmgr.so ${librrprotocol} ${fwdsp_real_objs}
	@echo "[link] $< => $@"
	@${CC}  -o $@ ${fwdsp_real_objs} -lrustyaxe ${LDFLAGS} ${LDFLAGS_FWDSP} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

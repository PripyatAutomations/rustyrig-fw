# Here we build fwdsp, which provides access to gstreamer pipelines
# from rrserver or rrclient
# - Eventually on embedded targets this will be replaced with
# a codec chip that supports a few common codecs.

# fwdsp uses the shared [fwdsp]/[pipelines] config callbacks from
# librrprotocol, but does not use libfwdspmgr itself at runtime.

CFLAGS_FWDSP := ${CFLAGS} -I${BUILD_DIR} ${gst_cflags}
LDFLAGS_FWDSP := ${LDFLAGS} -L. -lrustyaxe ${gst_ldflags} -lpthread -lrrprotocol
fwdsp := bin/fwdsp
bins += ${fwdsp}

fwdsp_objs := defconfig.o fwdsp.o
fwdsp_real_objs := $(addprefix ${BUILD_DIR}/fwdsp/,$(fwdsp_objs))

${BUILD_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS} GNUmakefile fwdsp/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(dir $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${fwdsp_real_objs}
	@mkdir -p $(dir $@)
	@echo "[link] $(words ${fwdsp_real_objs}) objects => $@"
	@${CC} -o $@ ${fwdsp_real_objs} ${LDFLAGS_FWDSP}
	@ls -a1ls $@
	@file $@
	@size $@

${BUILD_DIR}/fwdsp/cfg.fwdsp.o: librrprotocol/cfg.fwdsp.c GNUmakefile fwdsp/rules.mk
	@${RM} -f $@
	@mkdir -p $(dir $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

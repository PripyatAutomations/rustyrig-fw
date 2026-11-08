# Here we build fwdsp, which provides access to gstreamer pipelines
# from rrserver or rrclient
# - Eventually on embedded targets this will be replaced with
# a codec chip that supports a few common codecs.

# fwdsp uses the shared [fwdsp]/[pipelines] config callbacks from
# librrprotocol, but does not use libfwdspmgr itself at runtime.

CFLAGS_FWDSP = ${MEDIA_CFLAGS} -I${BUILD_DIR} ${gst_cflags}
# fwdsp objects are unsanitized by default, but shared librustyaxe/librrprotocol
# can be ASan/UBSan-instrumented. Link the matching runtimes into the executable
# so their interceptors are available from process startup.
FWDSP_SANITIZER_RUNTIME_LDFLAGS = $(filter -fsanitize=%,$(LDFLAGS))
LDFLAGS_FWDSP = ${MEDIA_LDFLAGS} ${FWDSP_SANITIZER_RUNTIME_LDFLAGS} -L. -lrustyaxe ${gst_ldflags} -lpthread -lrrprotocol
fwdsp := bin/fwdsp
bins += ${fwdsp}

fwdsp_objs := defconfig.o fwdsp.o
fwdsp_real_objs := $(addprefix ${BUILD_DIR}/fwdsp/,$(fwdsp_objs))

# Only fwdsp.c needs VERSION from the command line. defconfig.c gets it
# from generated build_config.h, avoiding a macro redefinition.
${BUILD_DIR}/fwdsp/fwdsp.o: CFLAGS_FWDSP += -DVERSION=\"$(VERSION)\"

# fwdsp/defconfig.c includes build_config.h, which pack-eeprom generates; wait
# for it before compiling so parallel builds cannot race the generator.
${BUILD_DIR}/fwdsp/%.o: fwdsp/%.c .version ${BUILD_HEADERS} GNUmakefile fwdsp/rules.mk ${librustyaxe_headers} ${librrprotocol_headers} ${OBJECT_ORDER_ONLY}
	@${RM} -f $@
	@mkdir -p $(dir $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${fwdsp_real_objs}
	@mkdir -p $(dir $@)
	@echo "[link] $(words ${fwdsp_real_objs}) objects => $@"
	@${CC} -o $@ ${fwdsp_real_objs} ${LDFLAGS_FWDSP}
	@ls -a1ls $@
	@file $@
	@size $@


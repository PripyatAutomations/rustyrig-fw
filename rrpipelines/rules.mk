rrpipelines := bin/rrpipelines
rrpipelines_objs := main.o pipeline.o
rrpipelines_real_objs := $(addprefix ${BUILD_DIR}/rrpipelines/,$(rrpipelines_objs))
rrpipelines_cflags := $(shell pkg-config --cflags gtk+-3.0 gstreamer-1.0)
rrpipelines_ldflags := $(shell pkg-config --libs gtk+-3.0 gstreamer-1.0)
bins += ${rrpipelines}
extra_clean += ${rrpipelines_real_objs} ${rrpipelines}

${BUILD_DIR}/rrpipelines/%.o: rrpipelines/%.c rrpipelines/pipeline.h ${BUILD_HEADERS} GNUmakefile rrpipelines/rules.mk ${librustyaxe_headers} ${OBJECT_ORDER_ONLY}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile-rrpipelines] $< => $@"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${rrpipelines_cflags} -o $@ -c $< || exit 2

${rrpipelines}: ${BUILD_HEADERS} ${librustyaxe} ${rrpipelines_real_objs}
	@echo "[link] $@ from $(words ${rrpipelines_real_objs}) objects"
	@${CC} -o $@ ${rrpipelines_real_objs} ${rrpipelines_ldflags} -lrustyaxe ${LDFLAGS} || exit 2
	@ls -a1ls $@
	@file $@

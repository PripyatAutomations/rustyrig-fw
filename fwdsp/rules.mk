fwdsp_objs += fwdsp-main.o

fwdsp_real_objs := $(foreach x, ${fwdsp_objs}, ${OBJ_DIR}/fwdsp/${x})

bin/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${fwdsp_real_objs}
	${CC} -o $@ ${fwdsp_real_objs} ${gst_ldflags} ${LDFLAGS} ${LDFLAGS_FWDSP} -lrustyaxe -lmongoose
	@ls -a1ls $@
	@file $@
	@size $@

${OBJ_DIR}/fwdsp/%.o: fwdsp/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_FWDSP} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

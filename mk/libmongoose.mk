${OBJ_DIR}/librustyaxe/mongoose.o: ext/libmongoose/mongoose.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] mongoose from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/mongoose.o: ../ext/libmongoose/mongoose.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] mongoose from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

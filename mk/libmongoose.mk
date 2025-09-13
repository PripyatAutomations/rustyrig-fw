libmongoose := libmongoose.so
extra_clean += ${libmongoose}

${libmongoose}: ${OBJ_DIR}/mongoose.o
	@echo "[link] $@ from $<"
	${CC}  -shared -fPIC -o $@ $^ -lmbedcrypto -lmbedtls -lmbedx509

${OBJ_DIR}/mongoose.o: ext/libmongoose/mongoose.c
	@echo "[compile] $@ from $<"
	${CC} ${CFLAGS} -o $@ -c $<

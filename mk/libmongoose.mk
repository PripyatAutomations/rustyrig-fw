libmongoose := libmongoose.so
extra_clean += ${libmongoose}

${libmongoose}: ${BUILD_DIR}/mongoose.o
	@echo "[link] $@ from $<"
	${CC}  -shared -fPIC -o $@ $^ -lmbedcrypto -lmbedtls -lmbedx509

${BUILD_DIR}/mongoose.o: ext/libmongoose/mongoose.c
	@echo "[compile] $@ from $<"
	${CC} ${CFLAGS} -o $@ -c $<

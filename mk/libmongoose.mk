libmongoose := libmongoose.so

${libmongoose}: ${BUILD_DIR}/mongoose.o
	@echo "[link] $@ from $<"
	${CC} ${LDFLAGS} -lmbedcrypto -lmbedtls -lmbedx509 -shared -fPIC -o $@ $^

${BUILD_DIR}/mongoose.o: ext/libmongoose/mongoose.c
	@echo "[compile] $@ from $<"
	${CC} ${CFLAGS} -o $@ -c $<

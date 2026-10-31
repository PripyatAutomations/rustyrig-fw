# Standalone SDR utility library. Pure C, no GStreamer or SDR hardware SDK.
librrsdrutils := librrsdrutils.so
librrsdrutils_objs := ${BUILD_DIR}/librrsdrutils/descriptor.o
libs += ${librrsdrutils}
extra_clean += ${librrsdrutils_objs} ${librrsdrutils} librrsdrutils.so.0 ${BUILD_DIR}/librrsdrutils/test-descriptor

${BUILD_DIR}/librrsdrutils/descriptor.o: librrsdrutils/descriptor.c librrsdrutils/rrsdrutils.h GNUmakefile ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${MEDIA_CFLAGS} -fPIC -c -o $@ $<

${librrsdrutils}: ${librrsdrutils_objs}
	@${CC} ${LIB_LDFLAGS} -Wl,-soname,librrsdrutils.so.0 -o $@ ${librrsdrutils_objs}
	@ln -sf librrsdrutils.so librrsdrutils.so.0

.PHONY: test-rrsdrutils
test-rrsdrutils: ${librrsdrutils}
	@mkdir -p ${BUILD_DIR}/librrsdrutils
	@${CC} ${MEDIA_CFLAGS} -Wall -Wextra -Werror -I. -o ${BUILD_DIR}/librrsdrutils/test-descriptor librrsdrutils/tests/descriptor.c -L. -Wl,-rpath,"$(CURDIR)" -lrrsdrutils
	@${BUILD_DIR}/librrsdrutils/test-descriptor

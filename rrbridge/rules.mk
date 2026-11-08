# KiwiSDR sound/control and waterfall use RustyAxe's native OpenSSL/wslay
# rr_transport client. No Mongoose transport exists in this bridge.
ifneq ($(OS),DARWIN)
ifeq (${PLATFORM},posix)
rrbridge_modules := bin/rrbridge-kiwi.so
bins += ${rrbridge_modules}
extra_clean += ${BUILD_DIR}/rrbridge/module.kiwi.o ${rrbridge_modules}
${BUILD_DIR}/rrbridge/module.kiwi.o: rrbridge/module.kiwi.c librrprotocol/bridge.sdr.h librrprotocol/bridge.ipc.h ${BUILD_HEADERS} ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${MEDIA_CFLAGS} -fPIC -c -o $@ $<
bin/rrbridge-kiwi.so: ${BUILD_DIR}/rrbridge/module.kiwi.o ${librrprotocol} ${librustyaxe}
	@mkdir -p bin
	@${CC} ${LIB_LDFLAGS} -o $@ $< -lrrprotocol -lrustyaxe ${MEDIA_LDFLAGS}
endif
endif

.PHONY: test-rrbridge-kiwi
test-rrbridge-kiwi:
	@mkdir -p ${BUILD_DIR}/rrbridge
	@${CC} ${MEDIA_CFLAGS} -Wall -Wextra -Werror -I. -o ${BUILD_DIR}/rrbridge/test.kiwi rrbridge/module.kiwi.c rrbridge/tests/kiwi.c -L. -Wl,-rpath,"${CURDIR}" -lrrprotocol -lrustyaxe ${MEDIA_LDFLAGS}
	@${BUILD_DIR}/rrbridge/test.kiwi

# Bridge host uses the same named-section parser as rrserver (one source of truth).
ifneq ($(OS),DARWIN)
ifeq (${PLATFORM},posix)
rrbridge_host_objs := ${BUILD_DIR}/rrbridge/main.o
bins += bin/rrbridge
extra_clean += ${rrbridge_host_objs} bin/rrbridge
${BUILD_DIR}/rrbridge/main.o: rrbridge/main.c librrprotocol/bridge.sdr.h librrprotocol/bridge.ipc.h ${BUILD_HEADERS} ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${MEDIA_CFLAGS} -c -o $@ $<
bin/rrbridge: ${rrbridge_host_objs} ${librustyaxe} ${librrprotocol} ${librrsdrutils}
	@mkdir -p bin
	@${CC} -o $@ ${rrbridge_host_objs} -lrrprotocol -lrustyaxe ${MEDIA_LDFLAGS}
endif
endif

.PHONY: check-rrbridge-native
check-rrbridge-native:
	@! grep -En 'mongoose\.h|\bmg_[[:alnum:]_]+\b|\bMG_EV_' rrbridge/main.c rrbridge/module.kiwi.c || { echo 'Unexpected Mongoose dependency in rrbridge' >&2; exit 1; }

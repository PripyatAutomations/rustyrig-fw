# KiwiSDR loadable receiver module. Runtime host will be added separately.
ifeq (${PLATFORM},posix)
rrbridge_modules := bin/rrbridge-kiwi.so
bins += ${rrbridge_modules}
extra_clean += ${BUILD_DIR}/rrbridge/module.kiwi.o ${rrbridge_modules}
${BUILD_DIR}/rrbridge/module.kiwi.o: rrbridge/module.kiwi.c librrprotocol/bridge.sdr.h ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${CFLAGS} -fPIC -c -o $@ $<
bin/rrbridge-kiwi.so: ${BUILD_DIR}/rrbridge/module.kiwi.o ${librrprotocol} ${librustyaxe}
	@mkdir -p bin
	@${CC} ${LIB_LDFLAGS} -o $@ $< -lrrprotocol -lrustyaxe ${LDFLAGS}
endif

.PHONY: test-rrbridge-kiwi
test-rrbridge-kiwi:
	@mkdir -p ${BUILD_DIR}/rrbridge
	@${CC} -std=c11 -Wall -Wextra -Werror -I. -o ${BUILD_DIR}/rrbridge/test.kiwi librrprotocol/bridge.sdr.c rrbridge/module.kiwi.c rrbridge/test.kiwi.c
	@${BUILD_DIR}/rrbridge/test.kiwi

# Bridge host uses the same named-section parser as rrserver (one source of truth).
ifeq (${PLATFORM},posix)
rrbridge_host_objs := ${BUILD_DIR}/rrbridge/main.o ${BUILD_DIR}/rrbridge/cfg.bridge.o
bins += bin/rrbridge
extra_clean += ${rrbridge_host_objs} bin/rrbridge
${BUILD_DIR}/rrbridge/main.o: rrbridge/main.c librrprotocol/bridge.sdr.h ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${CFLAGS} -c -o $@ $<
${BUILD_DIR}/rrbridge/cfg.bridge.o: rrserver/cfg.bridge.c rrserver/cfg.bridge.h ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@${CC} ${CFLAGS} -c -o $@ $<
bin/rrbridge: ${rrbridge_host_objs} ${librustyaxe} ${librrprotocol}
	@mkdir -p bin
	@${CC} -o $@ ${rrbridge_host_objs} -lrrprotocol -lrustyaxe ${LDFLAGS}
endif

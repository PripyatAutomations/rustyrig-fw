all: world

CFLAGS := -Wall -O2 -fPIC -ggdb -g -I../ -I${BUILD_DIR}/ -Wno-unused
LDFLAGS := -shared
CFLAGS += $(shell pkg-config --cflags mbedtls)
LDFLAGS += $(shell pkg-config --libs mbedtls mbedcrypto mbedx509)

librustyaxe := ../librustyaxe.so
libs := ${librustyaxe}

include rules.mk

${librustyaxe}: ${real_librr_objs} ${librr_headers} GNUmakefile rules.mk
	@echo "[link] $@ from $(words ${real_librr_objs}) objects"
	@${CC} ${LDFLAGS} -o $@ ${real_librr_objs}

world: ${libs}

obj/%.o:%.c GNUmakefile ${librr_headers}
	@echo "[compile] $< => $@"
	@${RM} $@
	@${CC} ${CFLAGS} -o $@ -c $<

clean:
	@echo "[clean]"
	${RM} -f ${real_librr_objs} librustyaxe.so

distclean: clean
	@echo "[distclean]"

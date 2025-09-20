all: world

CFLAGS := -Wall -O2 -fPIC -ggdb -g -I../ -Wno-unused
LDFLAGS := -shared
CFLAGS += $(shell pkg-config --cflags mbedtls)
LDFLAGS += $(shell pkg-config --libs mbedtls mbedcrypto mbedx509)

include rules.mk

world: ${libs}

clean:
	@echo "[clean]"
	${RM} -f ${real_librustyrig_objs} ${librustyrig}

distclean: clean
	@echo "[distclean]"

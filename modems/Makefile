# Independent worker builds. Upstream source is cloned only when missing.
# Existing checkouts are NEVER reset, updated, or overwritten.
# Run 'make -C modems' explicitly. No network dependency for normal builds.
GIT ?= git
CMAKE ?= cmake
CC ?= cc
CXX ?= c++
EXT ?= ../ext
PREFIX ?= /var/lib/rustyrig/modems
PSK_SRC := $(EXT)/libpsk
OLIVIA_SRC := $(EXT)/olivia-modem
CFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++20

.PHONY: all clean install test
all: rrmodem-psk rrmodem-olivia

$(PSK_SRC)/CMakeLists.txt:
	@if test -e "$(PSK_SRC)"; then echo "Invalid existing libpsk checkout: $(PSK_SRC)" >&2; exit 1; fi
	$(GIT) clone https://github.com/jfrancis42/libpsk.git "$(PSK_SRC)"

$(OLIVIA_SRC)/CMakeLists.txt:
	@if test -e "$(OLIVIA_SRC)"; then echo "Invalid existing Olivia checkout: $(OLIVIA_SRC)" >&2; exit 1; fi
	$(GIT) clone https://github.com/jfrancis42/olivia-modem.git "$(OLIVIA_SRC)"

$(PSK_SRC)/build/libpsk.a: $(PSK_SRC)/CMakeLists.txt
	$(CMAKE) -S "$(PSK_SRC)" -B "$(PSK_SRC)/build"
	$(CMAKE) --build "$(PSK_SRC)/build" --target psk

$(OLIVIA_SRC)/build/liboliviamodem.a: $(OLIVIA_SRC)/CMakeLists.txt
	$(CMAKE) -S "$(OLIVIA_SRC)" -B "$(OLIVIA_SRC)/build"
	$(CMAKE) --build "$(OLIVIA_SRC)/build" --target oliviamodem

rrmodem-psk: psk/rrmodem-psk.c common/pcm16k.h $(PSK_SRC)/build/libpsk.a
	$(CC) $(CFLAGS) -I"$(PSK_SRC)/include" -o "$@" psk/rrmodem-psk.c "$(PSK_SRC)/build/libpsk.a" -lm

rrmodem-olivia: olivia/rrmodem-olivia.cpp common/pcm16k.h $(OLIVIA_SRC)/build/liboliviamodem.a
	$(CXX) $(CXXFLAGS) -I"$(OLIVIA_SRC)/include" -o "$@" olivia/rrmodem-olivia.cpp "$(OLIVIA_SRC)/build/liboliviamodem.a" -lm

test: all
	$(CMAKE) --build "$(PSK_SRC)/build"
	ctest --test-dir "$(PSK_SRC)/build" --output-on-failure

install: all
	install -d "$(DESTDIR)$(PREFIX)"
	install -m 0755 rrmodem-psk rrmodem-olivia "$(DESTDIR)$(PREFIX)/"

clean:
	rm -f rrmodem-psk rrmodem-olivia

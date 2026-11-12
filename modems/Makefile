# GNU Make builds for external PSK and Olivia workers.
# Upstreams live in ../ext and are never modified by this build.
# Missing sources are cloned once; existing checkouts are preserved.
.DEFAULT_GOAL := all

GIT ?= git
CC ?= cc
CXX ?= c++
AR ?= ar
EXT ?= ../ext
PREFIX ?= /var/lib/rustyrig/modems
BUILD_DIR ?= build
PSK_SRC := $(EXT)/libpsk
OLIVIA_SRC := $(EXT)/olivia-modem

CPPFLAGS ?=
CFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++20
LDFLAGS ?=
LDLIBS ?= -lm

PSK_SOURCES := varicode fir costas timing viterbi tx rx
PSK_OBJECTS := $(addprefix $(BUILD_DIR)/psk/,$(addsuffix .o,$(PSK_SOURCES)))
OLIVIA_OBJECT := $(BUILD_DIR)/olivia/oliviamodem.o
PSK_LIBRARY := $(BUILD_DIR)/libpsk.a
OLIVIA_LIBRARY := $(BUILD_DIR)/liboliviamodem.a
PSK_TESTS := $(addprefix $(BUILD_DIR)/tests/psk-,$(strip varicode tx loopback snr modes))

.PHONY: all test install clean fetch
all: rrmodem-psk rrmodem-olivia
fetch: $(PSK_SRC)/CMakeLists.txt $(OLIVIA_SRC)/CMakeLists.txt

# CMakeLists.txt is only a pre-existing checkout marker: cmake is NOT invoked.
$(PSK_SRC)/CMakeLists.txt:
	@if test -e "$(PSK_SRC)"; then echo "Incomplete existing libpsk checkout: $(PSK_SRC)" >&2; exit 1; fi
	@mkdir -p "$(EXT)"
	$(GIT) clone https://github.com/jfrancis42/libpsk.git "$(PSK_SRC)"

$(OLIVIA_SRC)/CMakeLists.txt:
	@if test -e "$(OLIVIA_SRC)"; then echo "Incomplete existing Olivia checkout: $(OLIVIA_SRC)" >&2; exit 1; fi
	@mkdir -p "$(EXT)"
	$(GIT) clone https://github.com/jfrancis42/olivia-modem.git "$(OLIVIA_SRC)"

# Missing upstream sources first trigger checkout. Never edit upstream files.
$(PSK_SRC)/src/%.c: $(PSK_SRC)/CMakeLists.txt
	@test -f "$@" || { echo "Missing upstream file: $@" >&2; exit 1; }
$(OLIVIA_SRC)/src/oliviamodem.cpp: $(OLIVIA_SRC)/CMakeLists.txt
	@test -f "$@" || { echo "Missing upstream file: $@" >&2; exit 1; }
$(PSK_SRC)/tests/test_%.c: $(PSK_SRC)/CMakeLists.txt
	@test -f "$@" || { echo "Missing upstream test: $@" >&2; exit 1; }

$(BUILD_DIR)/psk/%.o: $(PSK_SRC)/src/%.c | $(PSK_SRC)/CMakeLists.txt
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -I"$(PSK_SRC)/include" -I"$(PSK_SRC)/src" -c -o "$@" "$<"

$(BUILD_DIR)/olivia/oliviamodem.o: $(OLIVIA_SRC)/src/oliviamodem.cpp | $(OLIVIA_SRC)/CMakeLists.txt
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I"$(OLIVIA_SRC)/include" -c -o "$@" "$<"

$(PSK_LIBRARY): $(PSK_OBJECTS)
	@mkdir -p "$(@D)"
	$(AR) rcs "$@" $^

$(OLIVIA_LIBRARY): $(OLIVIA_OBJECT)
	@mkdir -p "$(@D)"
	$(AR) rcs "$@" $^

rrmodem-psk: psk/rrmodem-psk.c common/pcm16k.h $(PSK_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I"$(PSK_SRC)/include" $(LDFLAGS) -o "$@" psk/rrmodem-psk.c "$(PSK_LIBRARY)" $(LDLIBS)

rrmodem-olivia: olivia/rrmodem-olivia.cpp common/pcm16k.h $(OLIVIA_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I"$(OLIVIA_SRC)/include" $(LDFLAGS) -o "$@" olivia/rrmodem-olivia.cpp "$(OLIVIA_LIBRARY)" $(LDLIBS)

$(BUILD_DIR)/tests/psk-%: $(PSK_SRC)/tests/test_%.c $(PSK_LIBRARY)
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99 -I"$(PSK_SRC)/include" $(LDFLAGS) -o "$@" "$<" "$(PSK_LIBRARY)" $(LDLIBS)

test: all $(PSK_TESTS)
	@set -e; for t in $(PSK_TESTS); do echo "[TEST] $$t"; "./$$t"; done
	@./rrmodem-psk BPSK31 1500 4</dev/null >/dev/null
	@./rrmodem-olivia 8 500 4</dev/null >/dev/null

install: all
	install -d "$(DESTDIR)$(PREFIX)"
	install -m 0755 rrmodem-psk rrmodem-olivia "$(DESTDIR)$(PREFIX)/"

clean:
	rm -rf "$(BUILD_DIR)" rrmodem-psk rrmodem-olivia

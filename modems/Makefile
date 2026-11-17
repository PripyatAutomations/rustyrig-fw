# GNU Make builds for external PSK and Olivia workers.
# Upstreams live in ../ext and are never modified by this build.
# Missing sources are cloned once; existing checkouts are preserved.
.DEFAULT_GOAL := all

GIT ?= git
CC ?= cc
CXX ?= c++
AR ?= ar
EXT ?= ../ext
PREFIX ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec/rustyrig
MODEMDIR ?= $(LIBEXECDIR)/modems
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
FT8_LIB ?= ../ext/ft8_lib

PSK_TESTS := $(addprefix $(BUILD_DIR)/tests/psk-,$(strip varicode tx loopback snr modes))

.PHONY: all test test-snr ft8-snr test-ft8 install clean fetch
all: fetch rrmodem-psk rrmodem-olivia rrmodem-ft8
# Fetch once, before any parallel compile. CMake is not used by this build.
# Preserve existing files and user modifications; restore only *missing*
# files when the checkout's own HEAD tracks them.
fetch:
	@set -eu; \
	mkdir -p "$(EXT)"; \
	if test ! -e "$(PSK_SRC)"; then \
		$(GIT) clone https://github.com/jfrancis42/libpsk.git "$(PSK_SRC)"; \
	fi; \
	if test ! -e "$(OLIVIA_SRC)"; then \
		$(GIT) clone https://github.com/jfrancis42/olivia-modem.git "$(OLIVIA_SRC)"; \
	fi; \
	for name in $(PSK_SOURCES); do \
		file="src/$$name.c"; \
		if test -f "$(PSK_SRC)/$$file"; then continue; fi; \
		if $(GIT) -C "$(PSK_SRC)" ls-files --error-unmatch "$$file" >/dev/null 2>&1 && \
		   $(GIT) -C "$(PSK_SRC)" cat-file -e "HEAD:$$file" 2>/dev/null; then \
			echo "Restoring missing tracked libpsk file: $$file"; \
			$(GIT) -C "$(PSK_SRC)" restore --source=HEAD --worktree -- "$$file"; \
		fi; \
		if test ! -f "$(PSK_SRC)/$$file"; then \
			echo "libpsk is incomplete: missing $$file in $(PSK_SRC)" >&2; \
			echo "Check $(GIT) -C $(PSK_SRC) remote -v and status --short." >&2; \
			echo "This build does not overwrite or re-clone existing sources." >&2; \
			exit 1; \
		fi; \
	done; \
	if test ! -f "$(OLIVIA_SRC)/src/oliviamodem.cpp"; then \
		if $(GIT) -C "$(OLIVIA_SRC)" ls-files --error-unmatch src/oliviamodem.cpp >/dev/null 2>&1 && \
		   $(GIT) -C "$(OLIVIA_SRC)" cat-file -e HEAD:src/oliviamodem.cpp 2>/dev/null; then \
			$(GIT) -C "$(OLIVIA_SRC)" restore --source=HEAD --worktree -- src/oliviamodem.cpp; \
		fi; \
		test -f "$(OLIVIA_SRC)/src/oliviamodem.cpp" || { \
			echo "Incomplete Olivia checkout: $(OLIVIA_SRC)/src/oliviamodem.cpp" >&2; exit 1; \
		}; \
	fi; \
	if test ! -f "$(FT8_LIB)/Makefile"; then \
		if $(GIT) -C .. rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
			$(GIT) -C .. submodule update --init ext/ft8_lib; \
		elif test ! -e "$(FT8_LIB)"; then \
			$(GIT) clone https://github.com/kgoba/ft8_lib.git "$(FT8_LIB)"; \
		else \
			echo "Incomplete existing FT8 source tree: $(FT8_LIB)" >&2; exit 1; \
		fi; \
	fi

# A missing source triggers the same serialized validation/repair, not one
# parallel recipe per file and not a CMakeLists.txt existence check.
$(PSK_SRC)/src/%.c: | fetch
	@test -f "$@" || { echo "Missing libpsk source after fetch: $@" >&2; exit 1; }
$(OLIVIA_SRC)/src/oliviamodem.cpp: | fetch
	@test -f "$@" || { echo "Missing Olivia source after fetch: $@" >&2; exit 1; }
$(PSK_SRC)/tests/test_%.c: | fetch
	@test -f "$@" || { echo "Missing upstream libpsk test: $@" >&2; exit 1; }

$(BUILD_DIR)/psk/%.o: $(PSK_SRC)/src/%.c | fetch
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -I"$(PSK_SRC)/include" -I"$(PSK_SRC)/src" -c -o "$@" "$<"

$(BUILD_DIR)/olivia/oliviamodem.o: $(OLIVIA_SRC)/src/oliviamodem.cpp | fetch
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I"$(OLIVIA_SRC)/include" -c -o "$@" "$<"

$(PSK_LIBRARY): $(PSK_OBJECTS)
	@mkdir -p "$(@D)"
	$(AR) rcs "$@" $^

$(OLIVIA_LIBRARY): $(OLIVIA_OBJECT)
	@mkdir -p "$(@D)"
	$(AR) rcs "$@" $^

rrmodem-psk: psk/rrmodem-psk.c common/pcm16k.h $(PSK_LIBRARY)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -I"$(PSK_SRC)/include" $(LDFLAGS) -o "$@" psk/rrmodem-psk.c "$(PSK_LIBRARY)" $(LDLIBS)

rrmodem-olivia: olivia/rrmodem-olivia.cpp common/pcm16k.h $(OLIVIA_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -pthread -I"$(OLIVIA_SRC)/include" $(LDFLAGS) -o "$@" olivia/rrmodem-olivia.cpp "$(OLIVIA_LIBRARY)" $(LDLIBS)

rrmodem-ft8:
	@if test ! -f "$(FT8_LIB)/Makefile"; then \
		if $(GIT) -C .. rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
			$(GIT) -C .. submodule update --init ext/ft8_lib; \
		elif test ! -e "$(FT8_LIB)"; then \
			$(GIT) clone https://github.com/kgoba/ft8_lib.git "$(FT8_LIB)"; \
		else \
			echo "Incomplete existing FT8 source tree: $(FT8_LIB)" >&2; exit 1; \
		fi; \
	fi
	$(MAKE) -C ft8 FT8_LIB="$(abspath $(FT8_LIB))"

test-ft8: rrmodem-ft8
	@echo "FT8 worker built successfully"

$(BUILD_DIR)/tests/psk-%: $(PSK_SRC)/tests/test_%.c $(PSK_LIBRARY)
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -I"$(PSK_SRC)/include" $(LDFLAGS) -o "$@" "$<" "$(PSK_LIBRARY)" $(LDLIBS)

test: all $(PSK_TESTS)
	@set -e; for t in $(PSK_TESTS); do echo "[TEST] $$t"; "./$$t"; done
	@./rrmodem-psk BPSK31 1500 4</dev/null >/dev/null
	@./rrmodem-olivia 8 500 4</dev/null >/dev/null

$(BUILD_DIR)/tests/psk-snr-sweep: tests/psk_snr.c tests/snr_noise.h $(PSK_LIBRARY)
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -Itests -I"$(PSK_SRC)/include" $(LDFLAGS) -o "$@" "$<" "$(PSK_LIBRARY)" $(LDLIBS)

$(BUILD_DIR)/tests/olivia-snr-sweep: tests/olivia_snr.cpp tests/snr_noise.h $(OLIVIA_LIBRARY)
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Itests -I"$(OLIVIA_SRC)/include" $(LDFLAGS) -o "$@" "$<" "$(OLIVIA_LIBRARY)" $(LDLIBS)

# Benchmarks report decode probability by SNR; no arbitrary pass threshold.
test-snr: $(BUILD_DIR)/tests/psk-snr-sweep $(BUILD_DIR)/tests/olivia-snr-sweep
	./$(BUILD_DIR)/tests/psk-snr-sweep
	./$(BUILD_DIR)/tests/olivia-snr-sweep
	$(MAKE) ft8-snr

# Optional FT8 benchmark. ft8_lib is already a git submodule in ext/.
# The target initializes it if missing; does not reset existing checkouts.
ft8-snr:
	@if test ! -f "$(FT8_LIB)/Makefile"; then $(GIT) -C .. submodule update --init ext/ft8_lib; fi
	$(MAKE) -C "$(FT8_LIB)" gen_ft8 decode_ft8
	python3 tests/ft8_snr.py --ft8-lib "$(FT8_LIB)"

install: all
	install -d "$(DESTDIR)$(MODEMDIR)"
	install -m 0755 rrmodem-psk rrmodem-olivia ft8/rrmodem-ft8 "$(DESTDIR)$(MODEMDIR)/"

clean:
	rm -rf "$(BUILD_DIR)" rrmodem-psk rrmodem-olivia
	$(MAKE) -C ft8 clean

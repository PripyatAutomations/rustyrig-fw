# Receive-only FT8 worker built with the standard RustyRig modem set.
# ft8_lib is pinned as a git submodule in ext/ft8_lib. No CMake needed.
FT8_LIB ?= ../../ext/ft8_lib
CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -I$(FT8_LIB)
LIBS = $(FT8_LIB)/libft8.a $(FT8_LIB)/.build/fft/kiss_fft.o $(FT8_LIB)/.build/fft/kiss_fftr.o -lm

.PHONY: all clean ft8-upstream
all: rrmodem-ft8

# One recursive make owns all upstream outputs, including under make -j.
# Force a rebuild once when migrating from the old racing recipes.
FT8_TARGETS := libft8.a .build/fft/kiss_fft.o .build/fft/kiss_fftr.o
FT8_STAMP := $(FT8_LIB)/.build/.rustyrig-serialized-v1

ft8-upstream:
	@test -f "$(FT8_LIB)/Makefile" || { echo "Missing ft8_lib; run git submodule update --init ext/ft8_lib"; exit 1; }
	+$(MAKE) -C "$(FT8_LIB)" $(if $(wildcard $(FT8_STAMP)),,-B) $(FT8_TARGETS)
	@touch "$(FT8_STAMP)"

rrmodem-ft8: ft8-worker.c ft8-upstream
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -o $@ ft8-worker.c $(LIBS)

clean:
	rm -f rrmodem-ft8

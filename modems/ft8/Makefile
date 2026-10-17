# Optional receive-only FT8 worker; no ft8_lib dependency for normal builds.
# ft8_lib is pinned as a git submodule in ext/ft8_lib. No CMake needed.
FT8_LIB ?= ../../ext/ft8_lib
CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -I$(FT8_LIB)
LIBS = $(FT8_LIB)/libft8.a $(FT8_LIB)/.build/fft/kiss_fft.o $(FT8_LIB)/.build/fft/kiss_fftr.o -lm

.PHONY: all clean
all: rrmodem-ft8

$(FT8_LIB)/libft8.a:
	@test -f $(FT8_LIB)/Makefile || { echo "Missing ft8_lib; run git submodule update --init ext/ft8_lib"; exit 1; }
	$(MAKE) -C $(FT8_LIB) libft8.a

$(FT8_LIB)/.build/fft/kiss_fft.o $(FT8_LIB)/.build/fft/kiss_fftr.o:
	$(MAKE) -C $(FT8_LIB) decode_ft8

rrmodem-ft8: ft8-worker.c $(FT8_LIB)/libft8.a $(FT8_LIB)/.build/fft/kiss_fft.o $(FT8_LIB)/.build/fft/kiss_fftr.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -o $@ ft8-worker.c $(LIBS)

clean:
	rm -f rrmodem-ft8

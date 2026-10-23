CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -DSTANDALONE \
           $(shell pkg-config --cflags gtk+-3.0)
LDLIBS  := $(shell pkg-config --libs gtk+-3.0)

all: gmatrix

gmatrix: gtk.matrix.c gtk.matrix.h
	$(CC) $(CFLAGS) -o $@ gtk.matrix.c $(LDLIBS)

clean:
	rm -f gmatrix

.PHONY: all clean

CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

.PHONY: all clean
all: gmatrix

gmatrix: gmatrix.c gmatrix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) -DSTANDALONE -o $@ gmatrix.c $(LDFLAGS) $(GTK_LIBS)

clean:
	rm -f gmatrix

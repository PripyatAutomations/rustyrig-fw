# Debian staging for the standalone gmatrix program.
# This fragment lives with gmatrix so it can move into a submodule unchanged.
# Called from the parent source package with:
#   make -C gmatrix -f debian/package.mk install DEB_DESTDIR=../debian/gmatrix
.PHONY: install
DEB_DESTDIR ?= ../debian/gmatrix
install:
	$(MAKE) -f Makefile all
	install -Dm755 gmatrix "$(DEB_DESTDIR)/usr/bin/gmatrix"

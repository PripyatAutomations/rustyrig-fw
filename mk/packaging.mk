install-build-deps: rrserver-deps audit-deps
FAKEROOT=$(shell which fakeroot)

# dpkg-buildpackage does not inherit GNU make's jobserver automatically.
# Forward the outer `make -jN` count so package builds do not become serial.
DEB_JOBS := $(patsubst -j%,%,$(filter -j%,$(MAKEFLAGS)))
ifeq ($(strip $(DEB_JOBS)),)
DEB_JOBS := $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
endif

indent:
	./tools/indent.sh

deb debs:
	mkdir -p ../releases
	rm -f ../releases/*.deb
	${FAKEROOT} dpkg-buildpackage -us -uc -b -j$(DEB_JOBS)

	# XXX: we should get rid of this except in maintainer mode!
	# XXX: Or at least ask before installing them....
	./tools/deb-release-test.sh
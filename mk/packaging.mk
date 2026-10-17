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
	${FAKEROOT} dpkg-buildpackage -us -uc -b -j$(DEB_JOBS)

rpm rpms:
	@echo "Sorry, rpm packaging isn't ready yet"

arch archpkg:
	@echo "Sorry, arch packaging isn't ready yet"


#modem-debs:
#	WITH_FT8=yes dpkg-buildpackage -b -us -uc

install-modems:
	install -Dm755 modems/ft8/rrmodem-ft8 /var/lib/rustyrig/modems/rrmodem-ft8


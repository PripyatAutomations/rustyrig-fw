install-build-deps: rrserver-deps audit-deps
FAKEROOT=$(shell which fakeroot)
indent:
	./tools/indent.sh

deb debs:
	${FAKEROOT} dpkg-buildpackage -us -uc -b

install-build-deps: rrserver-deps audit-deps

indent:
	./tools/indent.sh

deb debs:
	dpkg-buildpackage -us -uc -b

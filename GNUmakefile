#subdirs += fwdsp
subdirs += rrclient
subdirs += rrserver

all clean deps distclean installworld:
	@for i in ${subdirs}; do \
	   ${MAKE} -C src/$$i $@; \
	done

rrserver:
	${MAKE} -C src/rrserver world

rrclient:
	${MAKE} -C src/rrclient world

w64-deps w64-installer:
	${MAKE} -C src/rrclient $@

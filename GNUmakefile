#subdirs += fwdsp
subdirs += rrclient
subdirs += rrserver

all clean deps distclean install w64-deps w64-installer world:
	@for i in ${subdirs}; do \
	   ${MAKE} -C src/$$i $@; \
	done

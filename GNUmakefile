UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
   OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
   OS := MSYS
else
   OS := POSIX
endif
ifeq (${OS},POSIX)
subdirs += fwdsp
subdirs += rrserver
endif
subdirs += rrclient


all clean deps distclean installworld:
	@for i in ${subdirs}; do \
	   ${MAKE} -C src/$$i $@; \
	done

fwdsp:
	${MAKE} -C src/fwdsp world

rrserver:
	${MAKE} -C src/rrserver world

rrclient:
	${MAKE} -C src/rrclient world

# For now we only build rrclient for windows
w64-deps w64-rrclient-installer w64-rrclient-portable:
	${MAKE} -C src/rrclient $@

win64-rrclient-all: w64-rrclient-installer w64-rrclient-portable
#win64-rrserver-all: w64-rrserver-installer w64-rrserver-portable
#win64-fwdsp-all: w64-fwdsp-installer w64-fwdsp-portable

UNAME_S := $(shell uname -s)
ifeq ($(findstring MINGW64_NT,$(UNAME_S)),MINGW64_NT)
   OS := MINGW64
else ifeq ($(findstring MSYS_NT,$(UNAME_S)),MSYS_NT)
   OS := MSYS
else
   OS := POSIX
endif

PROFILE ?= radio
CF := config/${PROFILE}.config.json
subdirs += rrclient

ifeq (${OS},POSIX)
subdirs += rrserver
subdirs += fwdsp
endif


all world clean deps install symtab:
	@for i in ${subdirs}; do \
	   ${MAKE} -C src/$$i $@; \
	done

distclean:
	${RM} -f audit-logs/*
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
win64-deps win64-installer win64-portable:
	@for i in ${subdirs}; do \
	   ${MAKE} -C src/$$i $@; \
	done

include mk/database.mk
include mk/audit.mk
include mk/json-config.mk

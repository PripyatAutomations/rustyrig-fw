# New and improved GNU makefile
all: world

librustyaxe := librustyaxe/librustyaxe.so

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
include mk/database.mk
include mk/libmongoose.mk
include mk/eeprom.mk
include mk/compile.mk

extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_clean += ${EEPROM_FILE}
extra_clean += firmware.log
extra_clean_targets += clean-librustyaxe

#BUILD_HEADERS += ${BUILD_DIR}/eeprom_layout.h
BUILD_HEADERS += $(wildcard inc/rrserver/*.h) $(wildcard inc/rrclient/*.h)
BUILD_HEADERS += $(wildcard inc/librustyaxe/*.h) $(wildcard ${BUILD_DIR}/*.h)

short_bins := rrclient rrserver # fwdsp
bins += $(foreach x, ${short_bins}, ${BUILD_DIR}/${x})
fwdsp_src = $(fwdsp_objs:.o=.c)
rrclient_src = $(rrclent_objs:.o=.c)
rrserver_src = $(rrserver_objs:.o=.c)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${BUILD_DIR}/.stamp:
	mkdir -p ${BUILD_DIR}
	touch $@

${BUILD_DIR}/fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${fwdsp_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_FWDSP} -lrustyaxe -lmongoose -o $@ ${fwdsp_objs} ${gst_ldflags}
	@ls -a1ls $@
	@file $@
	@size $@

${BUILD_DIR}/rrclient: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${rrclient_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_RRCLIENT} -o $@ ${rrclient_objs} -lrustyaxe -lmongoose ${gtk_ldflags} ${gst_ldflags}
	@ls -a1ls $@
	@file $@
	@size $@

${BUILD_DIR}/rrserver: ${BUILD_HEADERS} ${librustyaxe} ${libmongoose} ${rrserver_objs}
	${CC} ${LDFLAGS} ${LDFLAGS_RRSERVER} -lrustyaxe -lmongoose -o $@ ${rrserver_objs} 
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${bins}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

############# Wrapper ############
librustyaxe_src = $(wildcard librustyaxe/*.c) $(wildcard librustyaxe/*.h)

${librustyaxe}: ${librustyaxe_src}
	${MAKE} -C librustyaxe -j4 world

clean-librustyaxe:
	${MAKE} -C librustyaxe distclean

###############


${OBJ_DIR}/%.o: %.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $@ from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

include mk/resource.mk
include mk/debug.mk
include mk/rrclient.objs.mk
include mk/rrserver.objs.mk
include mk/install.mk
include mk/win64.mk
include mk/audit.mk
include mk/clean.mk
include mk/git.mk

# This needs to be at the bottom so we can get final environment
world: ${BUILD_DIR}/.stamp ${bins} ${extra_build}

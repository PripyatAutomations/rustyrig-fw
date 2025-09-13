# New and improved GNU make file
all: world

librustyaxe := librustyaxe/librustyaxe.so

VERSION=$(shell cat .version)
DATE=$(shell date +%Y%m%d)
INSTALLER=rrclient.win64.${DATE}.exe

include mk/json-config.mk
include mk/compile.mk
include mk/git.mk
include mk/database.mk
include mk/audit.mk
include mk/librustyaxe.mk
include mk/libmongoose.mk
include mk/eeprom.mk

extra_clean += firmware.log
extra_clean += $(wildcard ${BUILD_DIR}/*.h)
extra_clean += ${EEPROM_FILE}
extra_clean += firmware.log

#BUILD_HEADERS += ${BUILD_DIR}/eeprom_layout.h
BUILD_HEADERS += $(wildcard inc/rrserver/*.h) $(wildcard inc/rrclient/*.h)
BUILD_HEADERS += $(wildcard inc/librustyaxe/*.h) $(wildcard ${BUILD_DIR}/*.h)

fw_bin := ${BUILD_DIR}/rrserver
bins += ${fw_bin}
fw_src = $(fw_objs:.o=.c)

ifeq (${PLATFORM},posix)
LDFLAGS += -lgpiod
endif

real_comm_objs := $(foreach x, ${comm_objs}, ${OBJ_DIR}/librustyaxe/${x})
real_fw_objs := $(foreach x, ${fw_objs}, ${OBJ_DIR}/firmware/${x})
real_fw_objs += ${OBJ_DIR}/firmware/logger.o
real_fw_objs += ${OBJ_DIR}/firmware/config.o
src_files = $(objs:.o=.c)
real_objs := $(foreach x, ${objs}, ${OBJ_DIR}/${x})

###################################
${BUILD_DIR}/build_config.h: ${EEPROM_FILE}
${EEPROM_FILE}: ${CF} ${CHANNELS} $(wildcard res/*.json)

${OBJ_DIR}/firmware/%.o: %.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${fw_bin}: ${real_fw_objs} ${real_comm_objs}
	@echo "[Link] firmware ($@) from $(words ${real_fw_objs}) object files..."
	@${CC} -o $@ ${real_fw_objs} ${real_comm_objs} ${LDFLAGS}
	@ls -a1ls $@
	@file $@
	@size $@

install:
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	cp -av ${bins} ${INSTALL_DIR}/bin
#	cp -av archive-config.sh *-rigctld.sh killall.sh rrclient.sh test-run.sh ${INSTALL_DIR}/bin
#	cp -aiv config/${PROFILE}.*.json config/client.config.json ${INSTALL_DIR}/etc

config/http.users:
	@echo "*** You do not have a config/http.users so i've copied the default file from doc/"
	cp -i doc/http.users.example config/http.users

${OBJ_DIR}/%.o: %.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $@ from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/%.o: ../librustyaxe/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] shared $@ from $<"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/audio.o: audio.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] $@ from $<"
	@${CC} ${CFLAGS} ${extra_cflags} -o $@ -c $<

${bin}: ${real_objs} ext/libmongoose/mongoose.c
	@echo "[Link] $@ from $(words ${real_objs}) object files..."
	@${CC} -o $@ ${real_objs} ${LDFLAGS}
	@ls -a1ls $@
	@file $@
	@size $@

strip: ${fw_bin}
	@echo "[strip] ${bins}"
	@strip $^
	@ls -a1ls $^

convert-icon:
	convert res/rustyrig.png -define icon:auto-resize=16,32,48,64,128,256 res/rustyrig.ico

include mk/debug.mk
include mk/rrclient.objs.mk
include mk/rrserver.objs.mk
include mk/install.mk
include mk/win64.mk
include mk/clean.mk

# This needs to be at the bottom so we can get final environment
world: ${extra_build} ${bins}

fwdsp: ${BUILD_HEADERS} ${librustyaxe} ${fwdsp_objs}
	${CC} ${LDFLAGS} -o $@ $^

rrclient: ${BUILD_HEADERS} ${librustyaxe} ${rrclient_objs}
	${CC} ${LDFLAGS} -o $@ $^

rrserver: ${BUILD_HEADERS} ${librustyaxe} ${rrserver_objs}
	${CC} ${LDFLAGS} -o $@ $^

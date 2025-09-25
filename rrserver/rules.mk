rrserver := bin/rrserver
bins += ${rrserver}

#rrserver_objs += channels.o		# Channel Memories
#rrserver_objs += console.o		# Console support
rrserver_objs += database.o		# sqlite3 database stuff
rrserver_objs += defconfig.o		# Default configuration
rrserver_objs += faults.o		# Fault management/alerting
#rrserver_objs += gpio.o		# GPIO controls
rrserver_objs += help.o		# support for help menus from filesystem, if available
rrserver_objs += main.o			# main loop
rrserver_objs += protection.o		# Protection features
#rrserver_objs += ptt.o			# Push To Talk controls (GPIO, CAT, etc)
rrserver_objs += thermal.o		# Thermal management
rrserver_objs += timer.o			# Timers support

rrserver_real_objs := $(foreach x, ${rrserver_objs}, ${OBJ_DIR}/rrserver/${x})
extra_clean += ${rrserver_real_objs}

${OBJ_DIR}/rrserver/%.o: rrserver/%.c ${BUILD_HEADERS} GNUmakefile rrserver/rules.mk ${librustyaxe_headers} ${librrprotocol_headers}
	@${RM} -f $@
	@mkdir -p $(shell dirname $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS_RRSERVER} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 1

bin/rrserver: ${BUILD_HEADERS} ${librustyaxe} ${librrprotocol} ${libmongoose} ${rrserver_real_objs} ${MASTER_DB}
	@${CC}  -o $@ ${rrserver_real_objs} -lrustyaxe -lrrprotocol -lmongoose -lev ${LDFLAGS} ${LDFLAGS_RRSERVER} || exit 2
	@ls -a1ls $@
	@file $@
	@size $@

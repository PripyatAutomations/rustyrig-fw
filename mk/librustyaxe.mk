
librr_objs += audio.framing.o		# Audio framing stuff
librr_objs += cat.o			# CAT parsers
librr_objs += cat.kpa500.o		# amplifier control (KPA-500 mode)
librr_objs += cat.yaesu.o		# Yaesu CAT protocol
librr_objs += codecneg.o		# Codec negotation bits
librr_objs += debug.o			# Debug stuff
librr_objs += dict.o			# dictionary object
librr_objs += io.o			# Input/Output abstraction/portability
librr_objs += io.serial.o		# Serial port stuff
librr_objs += io.socket.o		# Socket operations
librr_objs += json.o			# turn json into dicts and dicts into json
librr_objs += module.o			# loadable module support
librr_objs += util.file.o		# Misc file functions
librr_objs += util.math.o		# Misc math functions
librr_objs += util.string.o		# String utility functions
librrproto_objs += ws.mediachan.o	# Media channel

${OBJ_DIR}/librustyaxe/%.o: librustyaxe/%.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] shared $@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/firmware/config.o: librustyaxe/config.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] server:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

${OBJ_DIR}/firmware/logger.o: librustyaxe/logger.c ${BUILD_HEADERS}
	@${RM} -f $@
	@echo "[compile] server:$@ from $<"
	@mkdir -p $(shell dirname $@)
	@${CC} ${CFLAGS} ${FWDSP_CFLAGS} ${extra_cflags} -o $@ -c $<

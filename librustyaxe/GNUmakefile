all: world

CFLAGS := -Wall -O2 -fPIC -ggdb -g -I../ -I${BUILD_DIR}/ -Wno-unused
LDFLAGS := -shared

libs := librustyaxe.so

# !ls *.c|sed 's/.c$/.o/g'|sed 's/^/librr_objs += /g'
librr_objs += audio.framing.o
librr_objs += cat.o
librr_objs += cat.kpa500.o
librr_objs += cat.yaesu.o
librr_objs += codecneg.o
librr_objs += config.o
librr_objs += debug.o
librr_objs += dict.o
librr_objs += io.o
librr_objs += io.serial.o
librr_objs += io.socket.o
librr_objs += json.o
librr_objs += logger.o
librr_objs += module.o
librr_objs += posix.o
librr_objs += util.file.o
librr_objs += util.math.o
librr_objs += util.string.o
librr_objs += ws.mediachan.o


librustyaxe.so: ${librr_objs}
	@echo "[link] $@ from $(words $^) objects"
	@${CC} ${LDFLAGS} -o $@ $^

world: ${libs}

%.o:%.c
	@echo "[compile] $@ => $<"
	@${CC} ${CFLAGS} -o $@ -c $<

clean:
	@echo "[clean]"
	${RM} -f ${librr_objs} librustyaxe.so

distclean: clean
	@echo "[distclean]"

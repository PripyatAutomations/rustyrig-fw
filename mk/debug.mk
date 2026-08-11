# Address sanitizer
#CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
#LDFLAGS +=

# Warning flags
#CFLAGS += -Wextra
#CFLAGS += -Wpedantic
#CFLAGS += -Wshadow 
#CFLAGS += -Wconversion
#CFLAGS += -Wsign-conversion
#CFLAGS += -Wstrict-prototypes
#CFLAGS += -Wmissing-prototypes
CFLAGS += -Wpointer-arith 
CFLAGS += -Wcast-align
CFLAGS += -Wundef
#CFLAGS += -Wformat=2
CFLAGS += -Wnull-dereference
CFLAGS +=  -Wwrite-strings

# This is noisy but will help us find things that need added to the headers!
#CFLAGS += -Wmissing-declarations 

# More warnings
CFLAGS += -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wrestrict

symtabs += $(foreach x, ${bins}, ${x}.symtab)

symtabs: ${symtabs}
	@echo "Built $(words ${symtabs}) symtabs"

%.symtab: %
	nm $< | grep -v '.gtk_*' | grep -v '.mg_*'|grep -v '.[Ur].' > $@

client-gdb:
	@echo "[gdb] rrclient"
	./test-client.sh gdb

client-valgrind:
	@echo "[valgrind] rrclient"
	./test-client.sh valgrind

server-gdb:
	@echo "[gdb] ${fw_bin}"
	./test-server.sh gdb

server-valgrind: ${fw_bin} ${EEPROM_FILE}
	@echo "[valgrind] ${fw_bin}"
	./test-server.sh valgrind

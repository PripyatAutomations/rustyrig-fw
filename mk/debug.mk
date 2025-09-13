
symtab: ${fw_bin}.symtab ${bin}.symtab 

${fw_bin}.symtab: ${fw_bin}
	nm ${fw_bin} | grep -v '.gtk_*' | grep -v '.mg_*'|grep -v '.[Ur].' > $@

${bin}.symtab: ${bin}
	nm ${bin} |grep -v '.gtk_*' | grep -v '.mg_*'|grep -v '.[Ur].' > $@

client-gdb:
	gdb build/client/rrclient -ex run

server-gdb:
	gdb build/radio/rrserver -ex run

server-valgrind: ${fw_bin} ${EEPROM_FILE}
	@echo "[valgrind] ${fw_bin}"
	./test-run.sh valgrind

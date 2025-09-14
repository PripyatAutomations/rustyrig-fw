
symtabs += $(foreach x, ${bins}, ${x}.symtab)

symtabs: ${symtabs}
	@echo "Built $(words ${symtabs}) symtabs"

%.symtab: %
	nm $< | grep -v '.gtk_*' | grep -v '.mg_*'|grep -v '.[Ur].' > $@

client-gdb:
	gdb ./bin/rrclient -ex run

server-gdb:
	gdb ./bin/rrserver -ex run

server-valgrind: ${fw_bin} ${EEPROM_FILE}
	@echo "[valgrind] ${fw_bin}"
	./test-run.sh valgrind

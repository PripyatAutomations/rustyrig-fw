rrcli := bin/rrcli
bins += ${rrcli}

rrcli_objs += cfg.network.o
rrcli_objs += cli.cmd.o
rrcli_objs += connman.o
rrcli_objs += irc.servers.o
rrcli_objs += main.o
rrcli_objs += m_privmsg.o

rrcli_real_objs := $(foreach x, ${rrcli_objs}, ${OBJ_DIR}/rrcli/${x})

${BUILD_DIR}/rrcli/.stamp:
	mkdir -p ${BUILD_DIR}/rrcli/
	touch ${BUILD_DIR}/rrcli/.stamp

${rrcli}: ${BUILD_DIR}/rrcli/.stamp ${rrcli_real_objs} ${librustyaxe} ${librrprotocol}
	@${RM} $@
	$(CC) -L. -o $@ ${rrcli_real_objs} -lrustyaxe -lrrprotocol -lm -lev -ltinfo $(LDFLAGS) 
	@ls -a1ls $@
	@file $@
	@size $@

${BUILD_DIR}/rrcli/%.o: rrcli/%.c $(wildcard librustyaxe/*.h)
	@${RM} $@
	@echo "[compile] $< => $@"
	@$(CC) $(CFLAGS) -I. -I.. -o $@ -c $<

extra_clean += ${rrcli} ${rrcli_real_objs}

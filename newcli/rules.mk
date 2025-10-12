bin/irc-test: ${BUILD_DIR}/irc-test.o ${librustyaxe}
	@${RM} $@
	$(CC) -L. -o $@ $< -lrustyaxe -lm -lev -ltinfo $(LDFLAGS) 

${BUILD_DIR}/irc-test.o: newcli/irc-test.c $(wildcard *.h)
	@${RM} $@
	@echo "[compile] $< => $@"
	@$(CC) $(CFLAGS) -I. -I.. -o $@ -c $<

extra_clean += irc-test ${BUILD_DIR}/irc-test.o

bins += bin/irc-test

all clean distclean install world:
	${MAKE} -C .. $@

ls-src:
	find . -type f -size +1k -name \*.c | xargs wc -c | head -n -1 | sort -n

ifeq (irc-test.c,$(wildcard irc-test.c))
irc-test: irc-test.o ../librustyaxe.so
	${RM} -f $@
	$(CC) $(LDFLAGS) -L.. -o $@ $< -lrustyaxe -lmongoose -lm

irc-test.o: irc-test.c $(wildcard *.h)
	${RM} -f $@
	$(CC) $(CFLAGS) -I. -I.. -o $@ -c $<

extra_clean += irc-test irc-test.o

../librustyaxe.so:
	${MAKE} -C ../librustyaxe -j4
endif

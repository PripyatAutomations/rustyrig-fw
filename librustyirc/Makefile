CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: librustyirc.a
irc.o: irc.c irc.h
	$(CC) $(CFLAGS) -c irc.c -o $@
librustyirc.a: irc.o
	ar rcs $@ $^
test: irc.c test.c irc.h
	$(CC) $(CFLAGS) irc.c test.c -o irc-test
	./irc-test
clean:
	rm -f irc.o irc-test librustyirc.a

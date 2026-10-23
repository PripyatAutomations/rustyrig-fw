CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
.PHONY: all test clean
all: librustyirc.a
connection.o: connection.c connection.h irc.h
	$(CC) $(CFLAGS) -c connection.c -o $@
irc.o: irc.c irc.h
	$(CC) $(CFLAGS) -c irc.c -o $@
librustyirc.a: irc.o connection.o
	ar rcs $@ $^
test: irc.c connection.c test.c irc.h connection.h
	$(CC) $(CFLAGS) irc.c connection.c test.c -o irc-test
	./irc-test
clean:
	rm -f irc.o connection.o irc-test librustyirc.a

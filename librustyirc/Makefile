CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC
# Override these for an installed librustyaxe SDK. No librrprotocol dependency.
AXE_CPPFLAGS ?= -I.. -I../build/$(or $(PROFILE),radio)
AXE_LDFLAGS ?= -L.. -Wl,-rpath,'$$ORIGIN/..'
SOURCES := irc.c connection.c irc.channel.c irc.modes.c irc.user.c irc.capab.c irc.commands.c irc.numerics.c irc.event.c irc.dispatch.c irc.audio.c irc.dcc.c
OBJECTS := $(SOURCES:.c=.o)
.PHONY: all test check clean
all: librustyirc.a
$(OBJECTS): $(wildcard *.h)
%.o: %.c
	$(CC) $(CPPFLAGS) $(AXE_CPPFLAGS) $(CFLAGS) -c $< -o $@
librustyirc.a: $(OBJECTS)
	$(AR) rcs $@ $^
test check: librustyirc.a
	$(CC) $(CPPFLAGS) $(AXE_CPPFLAGS) $(CFLAGS) tests/core.c librustyirc.a $(AXE_LDFLAGS) -lrustyaxe -o irc-test
	./irc-test
clean:
	rm -f $(OBJECTS) irc-test librustyirc.a

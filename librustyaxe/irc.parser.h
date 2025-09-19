#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/json.h>
#include <librustyaxe/irc.struct.h>

// belongs in parser.h
extern irc_message_t *irc_parse_message(const char *msg);
extern bool irc_dispatch_message(irc_callback_t *callbacks, irc_message_t *mp);
extern bool irc_callback(const char *msg);

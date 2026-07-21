//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__irc_parser_h)
#define	__irc_parser_h

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/json.h>
#include <librustyaxe/list.h>
#include <librustyaxe/irc.struct.h>

// Parse the irc message into tokens
extern irc_message_t *irc_parse_message(const char *msg);

// Dispatch the irc message to the appropriate handler
extern bool irc_dispatch_message(irc_conn_t *cptr, irc_message_t *mp);

// Handle an IRC message (parse and dispatch)
extern bool irc_process_message(irc_conn_t *cptr, const char *msg);

// Add a callback to the list
extern bool irc_register_callback(irc_callback_t *cb);

// Remove a callback from the list
extern bool irc_remove_callback(irc_callback_t *cb);

extern bool irc_register_default_callbacks(void);
extern bool irc_register_default_numeric_callbacks(void);

// XXX: this belongs elsewhere but where?
extern bool irc_set_conn_pool(rrlist_t *conn_list);

#endif	// !defined(__irc_parser_h)


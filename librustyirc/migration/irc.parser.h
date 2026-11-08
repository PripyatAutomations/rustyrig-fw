//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#if     !defined(__irc_parser_h)
#define __irc_parser_h

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/json.h>
#include <librustyaxe/list.h>

#include <librrprotocol/irc.types.h>

// Parse the irc message into tokens
extern irc_message_t *irc_parse_message(const char *msg);

// Dispatch the irc message to the appropriate handler
extern bool irc_dispatch_message(rrconn_t *cptr, irc_message_t *mp);

// Handle an IRC message (parse and dispatch)
extern bool irc_process_message(rrconn_t *cptr, const char *msg);

// Borrow a wire callback (false on success); duplicate registration is harmless.
extern bool irc_register_callback(rr_irc_callback_t *cb);

// Unlink a borrowed callback (false on success); caller retains its storage.
extern bool irc_remove_callback(rr_irc_callback_t *cb);

extern bool irc_register_default_callbacks(void);
extern bool irc_register_default_numeric_callbacks(void);

// XXX: this belongs elsewhere but where?
extern bool irc_set_conn_pool(rrlist_t *conn_list);

#endif // !defined(__irc_parser_h)

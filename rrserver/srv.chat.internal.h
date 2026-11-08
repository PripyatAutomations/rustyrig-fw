//
// srv.chat.internal.h: Private server chat helpers
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_PROTOCOL_SRV_CHAT_INTERNAL_H
#define RR_PROTOCOL_SRV_CHAT_INTERNAL_H
#include <librrprotocol/rrprotocol.h>
/* Caller owns the returned space-separated account room list. NULL on failure. */
char *ws_chat_whois_rooms(const rrconn_t *target);
#endif

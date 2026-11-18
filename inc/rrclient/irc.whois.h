//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_IRC_WHOIS_H
#define RRCLIENT_IRC_WHOIS_H
#include <librrprotocol/rrprotocol.h>

extern void rrclient_irc_query_opened(rrconn_t *connection, const char *nick);
extern bool rrclient_whois_send(rrconn_t *connection, const char *window, const char *nick);
extern const char *rrclient_irc_whois_window(rrconn_t *connection, const char *nick);
extern void rrclient_irc_whois_complete(rrconn_t *connection, const char *nick);
extern void rrclient_irc_whois_forget(rrconn_t *connection);
#endif

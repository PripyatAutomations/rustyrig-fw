#ifndef RRCLIENT_IRC_WHOIS_H
#define RRCLIENT_IRC_WHOIS_H
#include <librrprotocol/rrprotocol.h>

extern const char *rrclient_irc_whois_window(rrconn_t *connection, const char *nick);
extern void rrclient_irc_whois_complete(rrconn_t *connection, const char *nick);
extern void rrclient_irc_whois_forget(rrconn_t *connection);
#endif

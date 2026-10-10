#ifndef RRCLIENT_SOCKS_H
#define RRCLIENT_SOCKS_H
#include <time.h>
#include <librrprotocol/rrprotocol.h>

#ifdef USE_MONGOOSE

typedef struct {
   char url[1024];
   char host[256], user[256], pass[256];
   unsigned port, stage;
   time_t deadline;
} rrclient_socks_t;

/* Empty URL disables the proxy. Both schemes resolve destinations remotely. */
bool rrclient_socks_init(rrclient_socks_t *, const char *url, const char *user, const char *pass, const rr_server_url_t *destination);
/* 0 negotiating, 1 tunnel just opened, -1 failed. Consumes only SOCKS bytes. */
int rrclient_socks_event(rrclient_socks_t *, struct mg_connection *, int event);
#endif
#endif

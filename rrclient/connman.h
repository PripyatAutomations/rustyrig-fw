//
// rrclient/connman.h
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_connman_h)
#define	__rrclient_connman_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

// Connected sessions
extern char active_server[512];
extern rr_connection_t *active_connections;
extern bool disconnect_server(const char *server);
extern bool connect_server(const char *server);
extern bool ws_connected;
extern bool ws_tx_connected;
#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn, *ws_tx_conn;
#endif
extern bool server_ptt_state;
extern const char *get_server_property(const char *server, const char *prop);
extern bool connect_or_disconnect(const char *server);

extern bool config_network_cb(const char *path, int line, const char *section, const char *buf);

#endif // !defined(__rrclient_connman_h)

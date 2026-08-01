// librrprotocol/connman.h
// Shared connection manager types and API for librrprotocol

#ifndef __librrprotocol_connman_h
#define __librrprotocol_connman_h

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#if defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

enum rr_conn_type {
   RR_CONN_NONE = 0,
   RR_CONN_MONGOOSE,
};

struct rr_connection {
   char                 name[256];
   bool                 connected;
   bool                 ptt_active;
   enum rr_conn_type    *conn_type;
#if defined(USE_MONGOOSE)
   struct mg_connection *mg_conn;
   struct mg_connection *ws_conn;
   struct mg_connection *ws_tx_conn;
#endif
   time_t poll_block_expire, poll_block_delay;
   char session_token[HTTP_TOKEN_LEN+1];

   struct rr_connection *next;
};
typedef struct rr_connection rr_connection_t;

extern char active_server[512];
extern rr_connection_t *active_connections;
extern bool disconnect_server(const char *server);
extern bool connect_server(const char *server);
extern bool ws_connected;
extern bool ws_tx_connected;
#if defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn, *ws_tx_conn;
#endif
extern bool server_ptt_state;
extern const char *get_server_property(const char *server, const char *prop);

#if defined(USE_GTK)
/* GtkButton is only referenced when building GTK UI code */
#include <gtk/gtk.h>
extern bool connect_or_disconnect(const char *server, GtkButton *button);
#endif

extern void connman_autoconnect(void);

#endif // __librrprotocol_connman_h

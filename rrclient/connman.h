//
// rrclient/connman.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_connman_h)
#define __rrclient_connman_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "ext/libmongoose/mongoose.h"
#include "librustyaxe/config.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/http.h"

enum rr_conn_type {
   RR_CONN_NONE = 0,
   RR_CONN_MONGOOSE,				// mongoose socket
};

struct rr_connection {
   char                 name[256];	// server name
   bool			connected;	// Are we connected?
   bool			ptt_active;	// Is PTT raised?
   enum rr_conn_type	*conn_type;	// connection type		
   struct mg_connection *mg_conn;	// mongoose socket
   struct mg_connection *ws_conn,	// RX stream
                        *ws_tx_conn;	// TX stream

   time_t poll_block_expire, poll_block_delay;
   char session_token[HTTP_TOKEN_LEN+1];

   /////
   struct rr_connection *next;			// next socket
};
typedef struct rr_connection rr_connection_t;

// Connected sessions
extern char active_server[512];
extern rr_connection_t *active_connections;
extern bool disconnect_server(const char *server);
extern bool connect_server(const char *server);
extern bool ws_connected;
extern bool ws_tx_connected;
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern bool server_ptt_state;
extern const char *get_server_property(const char *server, const char *prop);

#if	defined(USE_GTK)
extern bool connect_or_disconnect(const char *server, GtkButton *button);
#endif


#endif	// !defined(__rrclient_connman_h)

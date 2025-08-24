//
// ws.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_ws_h)
#define __rrclient_ws_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "common/config.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/audio.h"
#include "../../ext/libmongoose/mongoose.h"
#include "common/codecneg.h"
#include "common/http.h"

//#define	DEBUG_WS_BINFRAMES		// turn this off soon

enum rr_conn_type {
   RR_CONN_NONE = 0,
   RR_CONN_MONGOOSE,				// mongoose socket
};

struct rr_connection {
   bool			connected;	// Are we connected?
   bool			ptt_active;	// Is PTT raised?
   enum rr_conn_type	*conn_type;		// connection type		
   struct mg_connection *mg_conn;		// mongoose socket
   struct mg_connection *ws_conn,	// RX stream
                        *ws_tx_conn;	// TX stream

   time_t poll_block_expire, poll_block_delay;
   char session_token[HTTP_TOKEN_LEN+1];

   /////
   struct rr_connection *next;			// next socket
};
typedef struct rr_connection rr_connection_t;

// Connected sessions
extern rr_connection_t *active_connections;
extern char active_server[512];

///////
extern void ws_handler(struct mg_connection *c, int ev, void *ev_data);
extern void ws_init(void);
extern void ws_fini(void);
extern bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);
extern bool ws_binframe_process(const char *data, size_t len);
extern bool disconnect_server(void);
extern bool connect_server(void);
extern bool prepare_msg(char *buf, size_t len, const char *fmt, ...);
extern const char *get_server_property(const char *server, const char *prop);
extern bool ws_send_error(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_alert(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_notice(http_client_t *cptr, const char *fmt, ...);

#if	defined(USE_GTK)
extern bool connect_or_disconnect(GtkButton *button);
#endif

// ws.audio.c
extern bool ws_audio_init(void);
extern bool ws_select_codec(struct mg_connection *c, const char *codec, bool is_tx);

// ws.chat.c

#endif	// !defined(__rrclient_ws_h)

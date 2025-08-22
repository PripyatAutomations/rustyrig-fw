// 
// gtk-client/ws.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

// At startup, we try to find the distribution's TLS certificate authority trust store
const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

extern dict *cfg;		// config.c
struct mg_mgr mgr;
const char *tls_ca_path = NULL;
struct mg_str tls_ca_path_str;
bool cfg_show_pings = true;			// set ui.show-pings=false in config to hide
extern dict *servers;

#if	defined(USE_GTK)
#include <gtk/gtk.h>
extern GtkWidget *main_window;
extern GtkWidget *tx_codec_combo, *rx_codec_combo;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
#endif	// defined(USE_GTK)

//////////////
bool cfg_http_debug_crazy = false;

//////////////////////////////////
// Support for multiple servers //
//////////////////////////////////
extern rr_connection_t *active_connections;


//// OLD way
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
char active_server[512];
bool ws_connected = false;	// Is RX stream connecte?
bool ws_tx_connected = false;	// Is TX stream connected?
struct mg_connection *ws_conn = NULL, *ws_tx_conn = NULL;
bool server_ptt_state = false;

////////////////////////////////
///// ws.*.c message handlers //
////////////////////////////////
extern bool ws_handle_alert_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_auth_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_error_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_hello_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_media_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_notice_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_ping_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_rigctl_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_syslog_msg(struct mg_connection *c, struct mg_ws_message *msg);
extern bool ws_handle_talk_msg(struct mg_connection *c, struct mg_ws_message *msg);

///////////////////////////////
// A better way to route ws //
///////////////////////////////
struct ws_msg_routes {
   const char *type;		// auth|ping|talk|cat|alert|error|hello etc
   bool (*cb)(/*struct mg_connection *c, struct mg_ws_message *msg*/);
};

struct ws_msg_routes ws_routes[] = {
   { .type = "alert",	.cb = ws_handle_alert_msg },
   { .type = "auth",	.cb = ws_handle_auth_msg },
   { .type = "cat",	.cb = ws_handle_rigctl_msg },
   { .type = "error",	.cb = ws_handle_error_msg },
   { .type = "hello",	.cb = ws_handle_hello_msg },
   { .type = "media",   .cb = ws_handle_media_msg },
   { .type = "notice",  .cb = ws_handle_notice_msg },
   { .type = "ping",    .cb = ws_handle_ping_msg },
   { .type = "syslog",  .cb = ws_handle_syslog_msg },
   { .type = "talk",	.cb = ws_handle_talk_msg },
   { .type = NULL,	.cb = NULL }
};

////

bool ws_handle_hello_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   struct mg_str msg_data = msg->data;

   if (mg_json_get(msg_data, "$.hello", NULL) > 0) {
      char *hello = mg_json_get_str(msg_data, "$.hello");
      ui_print("[%s] *** Server version: %s ***", get_chat_ts(), hello);
      free(hello);
   }
   return false;
}

static bool ws_txtframe_dispatch(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      return true;
   }

   struct ws_msg_routes *rp = ws_routes;
   int i = 0;
   char json_req[65];
   struct mg_str msg_data = msg->data;

   while (rp[i].type) {
      if (!rp[i].type &&!rp[i].cb) {
         break;
      }

      memset(json_req, 0, sizeof(json_req));
      snprintf(json_req, sizeof(json_req), "$.%s", rp[i].type);

      // see if this exists in the json
      if (mg_json_get(msg_data, json_req, NULL) > 0) {
         // Matched, dispatch the message
         if (cfg_http_debug_crazy && strcasecmp(rp[i].type, "cat") != 0 &&
             strcasecmp(rp[i].type, "ping") != 0) {
            Log(LOG_CRAZY, "ws.router", "Matched route #%d for message type %s", i, rp[i].type);
         }
         rp[i].cb(c, msg);
         return false;
      }
      i++;
   }

   Log(LOG_CRAZY, "ws.router", "No matches for message: %s", msg_data);
   return true;
}

bool ws_binframe_process(const char *data, size_t len) {
   if (!data || len <= 10) {			// no real packet will EVER be under 10 bytes, even a keep-alive
      return true;
   }

//#if	defined(DEBUG_WS_BINFRAMES)
   char hex[128] = {0};
   size_t n = len < 16 ? len : 16;
   for (size_t i = 0; i < n; i++) {
      snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", (unsigned char)data[i]);
   }
   Log(LOG_DEBUG, "http.ws", "binary: %zu bytes, hex: %s", len, hex);
//#endif

//   if (data[0] == 'A' && data[1] == 'U') {
      audio_process_frame(data, len);
//   }
   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg || !msg->data.buf) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got c <%x> msg <%x> data <%x>", c, msg , (msg ? msg->data.buf : NULL));
      return true;
   }

#if     defined(HTTP_DEBUG_CRAZY)
   if (cfg_http_debug_crazy) {
      // XXX: This should be moved to an option in config perhaps?
      Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
   }
#endif

   if (msg->flags & WEBSOCKET_OP_BINARY) {
      // Binary (audio, waterfall, etc) frames
      ws_binframe_process(msg->data.buf, msg->data.len);
   } else {
      // Text (mostly json) frames
      ws_txtframe_dispatch(c, msg);
   }
   return false;
}

void http_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_OPEN) {
#if	defined(HTTP_DEBUG_CRAZY)
      if (cfg_http_debug_crazy) {
         c->is_hexdumping = 1;
      }
#endif
      ws_conn = c; 
   } else if (ev == MG_EV_CONNECT) {
      const char *url = get_server_property(active_server, "server.url");
      ui_print("[%s] * Connected *", get_chat_ts());

      if (c->is_tls) {
         struct mg_tls_opts opts = { .name = mg_url_host(url) };

         if (tls_ca_path) {
            opts.ca = tls_ca_path_str;
         } else {
            Log(LOG_CRIT, "ws", "No tls_ca_path set!");
         }
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_WRITE) {
      // Handle writing audio frames one by one
   } else if (ev == MG_EV_WS_OPEN) {
      const char *login_user = get_server_property(active_server, "server.user");
      ws_connected = true;
      update_connection_button(true, conn_button);
      ui_print("[%s] *** Connection Upgraded to WebSocket ***", get_chat_ts());
#if	defined(USE_GTK)
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-active");
      gtk_style_context_remove_class(ctx, "ptt-idle");
#endif	// defined(USE_GTK)

      if (!login_user) {
         Log(LOG_CRIT, "core", "server.user not set in config!");
         return;
      }
      ws_send_hello(c);
      ws_send_login(c, login_user);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      ws_handle(c, wm);
   } else if (ev == MG_EV_ERROR) {
      ui_print("[%s] Socket error: %s", get_chat_ts(), (char *)ev_data);
   } else if (ev == MG_EV_CLOSE) {
      if (ws_connected) {
         ui_print("[%s] *** DISCONNECTED ***", get_chat_ts());
      }
      ws_connected = false;
      ws_conn = NULL;
      update_connection_button(false, conn_button);
#if	defined(USE_GTK)
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-idle");
      gtk_style_context_remove_class(ctx, "ptt-active");
#endif	// defined(USE_GTK)
      userlist_clear_all();
   }
}

void ws_init(void) {
   const char *debug = cfg_get_exp("debug.http");
   if (debug && (strcasecmp(debug, "true") == 0 ||
                 strcasecmp(debug, "yes") == 0)) {
      mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   } else {
      mg_log_set(MG_LL_ERROR);
   }
   free((void *)debug);
   const char *debug_crazy = cfg_get_exp("debug.http.crazy");
   if (debug_crazy && (strcasecmp(debug_crazy, "true") == 0 ||
                       strcasecmp(debug_crazy, "yes") == 0)) {
      cfg_http_debug_crazy = true;
   }
   free((void *)debug_crazy);
         
   mg_mgr_init(&mgr);

// XXX: Fix this
//   tls_ca_path = find_file_by_list(default_tls_ca_paths, sizeof(default_tls_ca_paths) / sizeof(char *));
   if (!tls_ca_path) {
      tls_ca_path = strdup("*");
   }

   if (tls_ca_path) {
      // turn it into a mongoose string
      tls_ca_path_str = mg_str(tls_ca_path);
      Log(LOG_DEBUG, "ws", "Setting TLS CA path to <%x> %s with target mg_str at <%x>", tls_ca_path, tls_ca_path, tls_ca_path_str);
   } else {
      Log(LOG_CRIT, "ws", "unable to find TLS CA file");
      exit(1);
   }

   cfg_show_pings = cfg_get_bool("ui.show-pings", false);
   Log(LOG_DEBUG, "ws", "ws_init finished");
}

void ws_fini(void) {
   mg_mgr_free(&mgr);
}

const char *get_server_property(const char *server, const char *prop) {
   char *rv = NULL;

   if (!server || !prop) {
      Log(LOG_CRIT, "ws", "get_server_prop with null server:<%x> or prop:<%x>");
      return NULL;
   }

   char fullkey[1024];
   memset(fullkey, 0, sizeof(fullkey));
   snprintf(fullkey, sizeof(fullkey), "%s.%s", server, prop);
   rv = dict_get(servers, fullkey, NULL);
#if	defined(DEBUG_CONFIG) || defined(DEBUG_CONFIG_SERVER)
   ui_print("Looking up server key: %s returned %s", fullkey, (rv ? rv : "NULL"));
   dict_dump(servers, stdout);
#endif
   return rv;
}

///////////////////////////////////////////////////////////
// Handle properly connect, disconnect, and error events //
///////////////////////////////////////////////////////////
bool disconnect_server(void) {
   if (ws_connected) {
      if (ws_conn) {
         ws_conn->is_closing = 1;
      }
      ws_connected = false;
      gtk_button_set_label(GTK_BUTTON(conn_button), "Connect");
      // XXX: im not sure this is acceptable
//      ws_conn = NULL;
      userlist_clear_all();
   }
   return false;
}

bool connect_server(void) {
   // This could recurse in an ugly way if not careful...
   if (active_server[0] == '\0') {
      show_server_chooser();
      return true;
   }

   const char *url = get_server_property(active_server, "server.url");

   if (url) {
// XXX:
#if	defined(USE_GTK)
      gtk_button_set_label(GTK_BUTTON(conn_button), "----------");
#endif	// defined(USE_GTK)
      ui_print("[%s] Connecting to %s", get_chat_ts(), url);

      ws_conn = mg_ws_connect(&mgr, url, http_handler, NULL, NULL);

      if (!ws_conn) {
         ui_print("[%s] Socket connect error", get_chat_ts());
      }
   } else {
      ui_print("[%s] * Server '%s' does not have a server.url configured! Check your config or maybe you mistyped it?", active_server);
   }

   return false;
}

#if	defined(USE_GTK)
bool connect_or_disconnect(GtkButton *button) {
   if (ws_connected) {
      disconnect_server();
   } else {
      connect_server();
   }
   return false;
}
#endif	// defined(USE_GTK)

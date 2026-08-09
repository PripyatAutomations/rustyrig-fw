//
// rrgtk/ws.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>

#if     defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif

// At startup, we try to find the distribution's TLS certificate authority trust
// store
const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

extern time_t now;
extern dict *cfg;                                // config.c
#if     defined(USE_MONGOOSE)
struct mg_mgr mgr;
struct mg_str tls_ca_path_str;
#endif
const char *tls_ca_path = NULL;
bool cfg_show_pings = true;                      // set ui.show-pings=false in
                                                 // config to hide
extern const char *server_name;          // XXX: This needs to go away when we
                                         // go
                                         // multi-server
extern bool ws_connected;
extern const char *get_server_property(const char *server, const char *prop);

bool cfg_http_debug_crazy = false;

//////////////////////
// Websocket router //
//////////////////////
#if     defined(USE_MONGOOSE)
extern bool ws_handle_alert_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_client_auth_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_error_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_hello_msg(struct mg_connection *c, dict *d);
//extern bool ws_handle_media_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_notice_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_ping_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_rigctl_cli_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_syslog_msg(struct mg_connection *c, dict *d);
extern bool ws_handle_talk_msg(struct mg_connection *c, dict *d);

struct ws_msg_routes {
   const char *type;             // auth|ping|talk|cat|alert|error|hello etc
   bool auth_reqd;               // Is this only for authenticated users?
   bool (*cb)(/*struct mg_connection *c, struct mg_ws_message *msg*/);
};

struct ws_msg_routes ws_routes_cli[] = {
   {
      .type = "alert", .cb = ws_handle_alert_msg
   },
   {
      .type = "auth", .cb = ws_handle_client_auth_msg
   },
   {
      .type = "cat", .cb = ws_handle_rigctl_cli_msg
   },
   {
      .type = "error", .cb = ws_handle_error_msg
   },
   {
      .type = "hello", .cb = ws_handle_hello_msg
   },
/*
   {
      .type = "media", .cb = ws_handle_media_msg
   },
*/
   {
      .type = "notice", .cb = ws_handle_notice_msg
   },
   {
      .type = "ping", .cb = ws_handle_ping_msg
   },
   {
      .type = "syslog", .cb = ws_handle_syslog_msg
   },
   {
      .type = "talk", .cb = ws_handle_talk_msg
   },
   {
      .type = NULL, .cb = NULL
   }
};

bool ws_handle_hello_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_DEBUG, "ws", "hello: c:<%p> d:<%p>", c, d);

      return true;
   }
   char *hello = dict_get(d, "hello", NULL);
   if (hello) {
//      ui_print("[%s] *** Server version: %s ***", get_chat_ts(now), hello);
   }
   return false;
}

static bool ws_txtframe_dispatch(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg) {
      Log(LOG_DEBUG, "ws", "txtframe_dispatch: c:<%p> msg:<%p>", c, msg);

      return true;
   }
   int i = 0;
   char json_req[65];
   struct mg_str msg_data = msg->data;

   // Copy to a null terminated buffer
   char buf[HTTP_WS_MAX_MSG + 1];
   memset( buf, 0, sizeof(buf) );
   memcpy(buf, msg_data.buf, msg_data.len);

   // and expand into a dict, which is freed in cleanup below
   dict *d = json2dict(buf);

   // Pointer to available routes
   struct ws_msg_routes *rp = ws_routes_cli;

   // Walk the table of handlers
   while (rp[i].type) {
      // End of table marker
      if (!rp[i].type && !rp[i].cb) {
         break;
      }
      memset( json_req, 0, sizeof(json_req) );
      snprintf(json_req, sizeof(json_req), "$.%s", rp[i].type);
      // see if this exists in the json
      if (mg_json_get(msg_data, json_req, NULL) > 0) {
         // Matched, dispatch the message
         if (cfg_http_debug_crazy && strcasecmp(rp[i].type, "cat") != 0 &&
             strcasecmp(rp[i].type, "ping") != 0) {
            Log(LOG_CRAZY, "ws.router", "Matched route #%d for message type %s", i, rp[i].type);
         }
         /* Emit a generic event for this raw websocket message type so other
          * parts of the system can listen to socket-level messages without
          * depending on the current in-process handlers. The existing handler
          * is still called afterwards for backward compatibility. */
         char evname[64];
         memset( evname, 0, sizeof(evname) );
         snprintf(evname, sizeof(evname), "ws.msg.%s", rp[i].type);
         event_emit(evname, NULL, d);

         /* Call existing handler to preserve current behavior, then free the
          * dict. */
         rp[i].cb(c, d);
         free(d);

         return false;
      }
      i++;
   }
   dict_free(d);

   Log(LOG_CRAZY, "ws.router", "No matches for message: %s", msg_data);

   return true;
}

#endif // defined(USE_MONGOOSE)

bool ws_binframe_process(const char *data, size_t len) {
   if (!data || len <= 10) {
      // no real packet will EVER be under 10 bytes, even a keep-alive
      Log(LOG_DEBUG, "ws", "binframe_process: data:<%p> len: %d", data, len);

      return true;
   }
#if     defined(DEBUG_WS_BINFRAMES)
   char hex[128] = {
      0
   };
   size_t n = len < 16 ? len : 16;
   for (size_t i = 0 ; i < n ; i++) {
      snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", (unsigned char)data[i]);
   }
   Log(LOG_DEBUG, "http.ws", "binary: %zu bytes, hex: %s", len, hex);
#endif

//   audio_process_frame(data, len);
   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//

#if     defined(USE_MONGOOSE)
bool ws_handle_cli(struct mg_connection *c, struct mg_ws_message *msg) {
   if (!c || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got c <%p> msg <%p> data <%p>", c, msg,
         (msg ? msg->data.buf : NULL) );

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
   if (!c) {
      Log(LOG_DEBUG, "ws", "binframe_process: c:<%p> ev: %d ev_data:<%p>", c, ev, ev_data);

      return;
   }
   if (ev == MG_EV_OPEN) {
#if     defined(HTTP_DEBUG_CRAZY)
      if (cfg_http_debug_crazy) {
         c->is_hexdumping = 1;
      }
#endif
// XXX: readd this
//      ws_conn = c;
   } else if (ev == MG_EV_CONNECT) {
      Log(LOG_CRAZY, "ws", "ev_ws_connect");
   } else if (ev == MG_EV_WRITE) {
      // Handle writing audio frames one by one
   } else if (ev == MG_EV_WS_OPEN) {
      const char *this_server = server_name;
      const char *url = get_server_property(this_server, "server.url");
      if (c->is_tls) {
         struct mg_tls_opts opts = {
            .name = mg_url_host(url)
         };
         if (tls_ca_path) {
            opts.ca = tls_ca_path_str;
         } else {
            Log(LOG_CRIT, "ws", "No tls_ca_path set!");
         }
         mg_tls_init(c, &opts);
      }
      ws_connected = true;
      event_emit("connected", NULL, NULL);

      const char *login_user = get_server_property(this_server, "server.user");
      Log(LOG_DEBUG, "ws", "ev_ws_connect: server: |%s| user: |%s|", server_name, login_user);
      if (!login_user) {
         Log(LOG_CRIT, "ws", "server.user not set in config!");

         return;
      }
      ws_send_hello(c);
      ws_send_login(c, login_user);

   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      if (wm) {
         ws_handle_cli(c, wm);
      }
   } else if (ev == MG_EV_ERROR) {
      event_emit("http.error", NULL, NULL);
//      ui_print("[%s] Socket error: %s", get_chat_ts(now), (char *)ev_data);
   } else if (ev == MG_EV_CLOSE) {
// XXX: readd this
//      ui_print("[%s] *** DISCONNECTED ***", get_chat_ts(now));
//      ws_connected = false;
//      ws_conn = NULL;
//      update_connection_button(false, conn_button);
      event_emit("goodbye", NULL, NULL);
// XXX: readd this
//      userlist_clear_all();
   }
}

#endif // defined(USE_MONGOOSE)
void ws_client_init(void) {
   const char *debug = cfg_get_exp("debug.http");
   if ( debug && (strcasecmp(debug, "true") == 0 ||
                  strcasecmp(debug, "yes") == 0) ) {
#if     defined(USE_MONGOOSE)
      mg_log_set(MG_LL_DEBUG);   // or MG_LL_VERBOSE for even more
#endif
   } else {
#if     defined(USE_MONGOOSE)
      mg_log_set(MG_LL_ERROR);
#endif
   }
   free( (void *)debug );
   const char *debug_crazy = cfg_get_exp("debug.http.crazy");
   if ( debug_crazy && (strcasecmp(debug_crazy, "true") == 0 ||
                        strcasecmp(debug_crazy, "yes") == 0) ) {
      cfg_http_debug_crazy = true;
   }
   free( (void *)debug_crazy );

#if     defined(USE_MONGOOSE)
   mg_mgr_init(&mgr);
#endif
// XXX: Fix this
//   tls_ca_path = find_file_by_list(default_tls_ca_paths,
// sizeof(default_tls_ca_paths) / sizeof(char *));
   if (!tls_ca_path) {
      tls_ca_path = strdup("*");
   }
   if (tls_ca_path) {
#if     defined(USE_MONGOOSE)
      // turn it into a mongoose string
      tls_ca_path_str = mg_str(tls_ca_path);
      Log(LOG_DEBUG, "ws", "Setting TLS CA path to <%p> %s with target mg_str at <%p>", tls_ca_path,
         tls_ca_path, tls_ca_path_str);
#endif
   } else {
      Log(LOG_CRIT, "ws", "unable to find TLS CA file");
      exit(1);
   }
   cfg_show_pings = cfg_get_bool("ui.show-pings", false);
   Log(LOG_DEBUG, "ws", "ws_init finished");
}



// XXX: We need to move to a similar arrangement as the client,
// XXX: so these can be properly split across multiple source files
// XXX: and accessed in a pleasant way...
#if     defined(USE_MONGOOSE)
struct ws_msg_routes ws_routes[] = {
   {
      .type = "auth", .cb = ws_handle_auth_msg, .auth_reqd = false
   },
//   { .type = "cat",   .cb = ws_handle_cat_msg,   .auth_reqd = true },
//   { .type = "hello", .cb = ws_handle_hello_msg,  .auth_reqd = false },
//   { .type = "media", .cb = ws_handle_media_msg, .auth_reqd = true },
//   { .type = "ping",  .cb = ws_handle_ping_msg,  .auth_reqd = false },
//   { .type = "pong",  .cb = ws_handle_pong_msg,  .auth_reqd = false },
//   { .type = "talk",  .cb = ws_handle_talk_msg,  .auth_reqd = true },
//   { .type = "talk.cmd", .cb = ws_handle_talk_cmd, .auth_reqd = false },
//   { .type = "talk.quit", .cb = ws_handle_quit,  .auth_reqd = false },
};
#endif

bool rrproto_ws_connect(int server) {
   return false;
}

#if     defined(USE_MONGOOSE)
bool ws_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");

      return true;
   }
   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");

   return false;
}

// Send to a specific, authenticated websocket session
void ws_send_to_cptr(struct mg_connection *sender, http_client_t *cptr, struct mg_str *msg_data,
                     int data_type) {
   if (!cptr || !msg_data) {
      return;
   }
   mg_ws_send(cptr->conn, msg_data->buf, msg_data->len, data_type);
}

// Send to all logged in instances of the user
void ws_send_to_name(struct mg_connection *sender, const char *username, struct mg_str *msg_data,
                     int data_type) {
   if (!sender || !username || !msg_data) {
      Log(LOG_CRIT, "ws",
         "ws_send_to_name passed incomplete data; sender:<%p>, username:<%p>, msg_data:<%p>",
         sender, username, msg_data);

      return;
   }
   http_client_t *current = http_client_list;
   while (current) {
      // Messages from the server will have NULL sender
      if ( !sender || (current->is_ws && current->conn != sender) ) {
         ws_send_to_cptr(sender, current, msg_data, data_type);
      }
      current = current->next;
   }
}

#endif // defined(USE_MONGOOSE)

bool ws_kick_by_name(const char *name, const char *reason) {
   if (!http_client_list) {
      return true;
   }
   http_client_t *curr = http_client_list;
   while (curr) {
      if (strcasecmp(name, curr->chatname) == 0) {
#if     defined(USE_MONGOOSE)
         ws_kick_client(curr, reason);
#endif // USE_MONGOOSE
      }
      curr = curr->next;
   }
   return false;
}

bool ws_kick_by_uid(int uid, const char *reason) {
   if (!http_client_list) {
      return true;
   }
   http_client_t *curr = http_client_list;
   while (curr) {
      if (uid == curr->user->uid) {
#if     defined(USE_MONGOOSE)
         ws_kick_client(curr, reason);
#endif // defined(USE_MONGOOSE)

      }
      curr = curr->next;
   }
   return false;
}

bool ws_kick_client(http_client_t *cptr, const char *reason) {
   // skip freeing resources if no client structure
   if (!cptr) {
      Log( LOG_DEBUG, "auth", "ws_kick_client with NULL cptr and reason: %s",
         (reason ? reason : "(none)") );

      return true;
   }
#if     defined(USE_MONGOOSE)
   if (!cptr->conn) {
      Log( LOG_DEBUG, "auth", "ws_kick_client for cptr <%p> has mg_conn <%p> and is invalid", cptr,
         (cptr ? cptr->conn : NULL) );

      return true;
   }
#endif // defined(USE_MONGOOSE)
   // If we have a client structure attached, release it's resources
   if (cptr->user_agent) {
      free(cptr->user_agent);
      cptr->user_agent = NULL;
   }
   if (cptr->cli_version) {
      free(cptr->cli_version);
      cptr->cli_version = NULL;
   }
   char resp_buf[HTTP_WS_MAX_MSG + 1];
#if     defined(USE_MONGOOSE)
   struct mg_connection *c = cptr->conn;
   // make sure we're not accessing unsafe memory
   if (cptr->user && cptr->chatname[0] != '\0') {
      if (cptr->active) {
// XXX: readd this
//         ws_send_notice(c, "You have been kicked from the server: %s",
// reason);

         // XXX: replace with ws_broadcast_quit(cptr);

         // blorp out a quit to all connected users
         const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "quit", VAL_STR, "talk.user",
            cptr->chatname, VAL_STR, "talk.reason", reason, VAL_ULONG, "talk.ts", now, VAL_INT,
            "talk.clones", cptr->user->clones - 1);
         struct mg_str ms = mg_str(jp);
         ws_broadcast(NULL, &ms, WEBSOCKET_OP_TEXT);
         free( (void *)jp );
      }
   }
   return ws_kick_client_by_c(cptr->conn, reason);
#endif // defined(USE_MONGOOSE)

   return true;
}

#if     defined(USE_MONGOOSE)
bool ws_kick_client_by_c(struct mg_connection *c, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG + 1];
   if (!c) {
      return true;
   }
   // Tell their client they've been disconnected
   prepare_msg( resp_buf, sizeof(resp_buf), "Client kicked: %s",
      (reason ? reason : "no reason given") );
   const char *jp = dict2json_mkstr(VAL_STR, "auth.error", resp_buf);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (void *)jp );
   mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);

   http_remove_client(c);

   return false;
}

static bool ws_handle_pong(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;
   char *ts = NULL;
   if (!c || !msg || !msg->data.buf) {
      Log( LOG_CRAZY, "http.ws", "ws_handle_pong got msg <%p> c <%p> data <%p>", msg, c,
         (msg ? msg->data.buf : NULL) );
      rv = true;
      goto cleanup;
   }
   char ip[INET6_ADDRSTRLEN];   // Buffer to hold IPv4 or IPv6 address
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop( AF_INET6, c->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &c->rem.addr.ip4, ip, sizeof(ip) );
   }
   http_client_t *cptr = http_find_client_by_c(c);
   if (!cptr) {
      char msgbuf[512];

      prepare_msg(msgbuf, sizeof(msgbuf), "Kicking client from %s:%d who has no cptr?!?!!?", ip,
         port);
      ws_kick_client_by_c(c, msgbuf);
      rv = true;
      goto cleanup;
   }
   struct mg_str msg_data = msg->data;
   if ( !( ts = mg_json_get_str(msg_data, "$.pong.ts") ) ) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = true;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%s|",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), ts);
   }
   char *endptr;
   errno = 0;
   time_t ts_t = strtoll(ts, &endptr, 10);
   if (errno == ERANGE || ts_t < 0 || ts_t > LONG_MAX || *endptr != '\0') {
      Log(LOG_WARN, "http.pong", "Got invalid ts |%s| from client <%p>", ts, c);
      rv = true;
      goto cleanup;
   }
   time_t ping_expiry = ts_t + HTTP_PING_TIME;
   if ( (ping_expiry) < now ) {
      Log(LOG_AUDIT, "http.pong",
         "Late ping for mg_conn:<%p> on cptr:<%p> from %s:%d ts: %li + %li (timeout) < now %li", c,
         cptr, ip, port, ts_t, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING expired");
      rv = true;
      goto cleanup;
   } else {
      // The pong response is valid, update the client's data
      cptr->last_heard = now;
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%li] and last_ping to 0",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), now);
   }
cleanup:
   free(ts);

   return rv;
}

// Deal with the binary requests
bool ws_binframe_process_mg(struct mg_connection *c, const char *buf, size_t len) {
   Log(LOG_DEBUG, "ws.binframe", "Binary frame of %li bytes", len);

   http_client_t *cptr = http_find_client_by_c(c);
   if (!cptr) {
      Log(LOG_CRIT, "ws.binframe",
         "Binary frame from client at <%p> with no http session. Ignoring!");

      return true;
   }
   // Here we need to pull out the channel ID and send it the users expecting
   // this codec
   if (len < 8) {
      // This frame is too small to contain meaningful data, discard it
      return true;
   }
   // Copy 4 bytes from the start of the buffer into a NULL-terminated string
   char codec[5];
   memset(codec, 0, 5);
   memcpy(codec, buf, 4);

   // Copy 4 bytes from the buffer into a NULL-terminated string for channel id
   char channel[5];
   memset(channel, 0, 5);
   memcpy(channel, buf, 4);

   // Determine where to send the message, by channel #
   int chan_num = atoi(channel);
   Log(LOG_DEBUG, "ws.binframe", "Got message with codec %s for channel %d", codec, channel);

   return false;
}

//
// Handle a TEXT ws message
//
static bool ws_txtframe_process(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;

   char buf[HTTP_WS_MAX_MSG + 1];
   memset( buf, 0, sizeof(buf) );
   memcpy(buf, msg_data.buf, msg_data.len);
//   fprintf(stderr, "recv ws => %s\n", buf);
   dict *d = json2dict(buf);

   char *cmd = dict_get(d, "talk.cmd", NULL);
   char *data = dict_get(d, "talk.data", NULL);
   char *target = dict_get(d, "talk.args.target", NULL);
   char *msg_type = dict_get(d, "type", NULL);
   char *hello = dict_get(d, "hello", NULL);
   char *ping = dict_get(d, "ping", NULL);

   bool result = false;

   // Update the last-heard time for the user
   http_client_t *cptr = http_find_client_by_c(c);
   if (!cptr) {
      Log(LOG_CRAZY, "ws", "message from unauthenticated user at c:<%p>", c);

      return true;
   }
   cptr->last_heard = now;
   // Handle ping messages
   if (ping) {
      time_t ping_ts = dict_get_time_t(d, "ping.ts", 0);
      if (ping_ts) {
         const char *jp = dict2json_mkstr(VAL_STR, "type", "pong", VAL_ULONG, "ts", ping_ts);
         mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free( (void *)jp );
      }
      goto cleanup;
   }
   // Handle pong messages (responses to server-initiated pings)
   time_t pong_ts = dict_get_time_t(d, "pong.ts", 0);
   if (pong_ts && cptr) {
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Received pong from user %s for ts:%li", cptr->chatname, pong_ts);
      goto cleanup;
   } else if (hello) {
      Log(LOG_DEBUG, "ws", "Got HELLO from client at mg_conn:<%p>: %s", c, hello);
      cptr->cli_version = malloc(HTTP_UA_LEN);
      if (cptr->cli_version) {
         memset(cptr->cli_version, 0, HTTP_UA_LEN);
         snprintf(cptr->cli_version, HTTP_UA_LEN, "%s", hello);
      }
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      result = ws_handle_rigctl_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      if (cmd) {
         result = ws_handle_chat_msg(c, d);
      }
#if	0	// codec
   } else if (mg_json_get(msg_data, "$.media", NULL) > 0) {
      char *media_cmd = dict_get(d, "media.cmd", NULL);
      char *media_codecs = dict_get(d, "media.codecs", NULL);
      // all packets need a command
      if (!media_cmd) {
         result = true;
         goto cleanup;
      }
      if (strcasecmp(media_cmd, "capab") == 0) {
         // Capability negotiation
         if (media_codecs) {
            const char *preferred = cfg_get_exp("codecs.allowed");
            if (!preferred) {
               Log(LOG_CRIT, "ws.media", "media.capab needs codecs.allowed set in config!");
               result = true;
               goto cleanup;
            }
            char *common = codec_filter_common(preferred, media_codecs);
            free( (char *)preferred );
            if (strlen(common) < 4) {
               free(common);
               result = true;
               goto cleanup;
            }
            char def_codec[5];
            memset(def_codec, 0, 5);
            snprintf(def_codec, sizeof(def_codec), "%s", common);
            Log(LOG_INFO, "ws.media",
               "Client %s <%p> supported codecs: %s, my preferred codecs: %s, common codecs: %s, negotiated default codec: %s",
               cptr->chatname, cptr, media_codecs, cfg_get("codecs.allowed"), common, def_codec);
            char msgbuf[HTTP_WS_MAX_MSG + 1];
            const char *jp = dict2json_mkstr(VAL_STR, "media.cmd", "isupport", VAL_STR,
               "media.codecs", common, VAL_STR, "media.preferred", def_codec, VAL_ULONG, "media.ts",
               now);
            mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (void *)jp );
            Log(LOG_DEBUG, "ws.media",
               "Sending supported codecs |%s| with preferred |%s| to client |%s|", common,
               def_codec, cptr->chatname);
            free(common);
         } else {
            Log(LOG_CRIT, "ws.media", "media.capab without payload");
         }
      } else if (strcasecmp(media_cmd, "codec") == 0) {
         if (cptr->chatname[0] == '\0') {
            result = true;
            goto cleanup;
         }
         char *media_codec = dict_get(d, "media.codec", NULL);
         char *media_channel = dict_get(d, "media.channel", NULL);
         if (media_codec && strlen(media_codec) == 4) {
            Log(LOG_DEBUG, "ws.media", "Selected %s codec %s.%s for user %s at cptr:<%p>",
               media_channel, media_codec, media_channel, cptr->chatname, cptr);
            struct fwdsp_subproc *codec_tx_subproc = NULL;
            struct fwdsp_subproc *codec_rx_subproc = NULL;
// XXX: Rewrite this to subscribe rx_channels and rx_channels
            if (media_channel) {
               // XXX: Should we store pointers to the subprocs in the user
               // struct? downside is it requires librustyaxe/http.h to include
               // rrserver/fwdsp-mgr.h or move struct fwdsp_subrpco to
               // librustyaxe/fwdsp-shared.h
               if (strcasecmp(media_channel, "tx") == 0) {
                  if (cptr->codec_tx[0] != '\0') {
                     // XXX: Decrease refcnt on old codec
                  }
                  memset( cptr->codec_tx, 0, sizeof(cptr->codec_tx) );
                  memcpy(cptr->codec_tx, media_codec, 4);
                  codec_tx_subproc = fwdsp_find_or_create(cptr->codec_tx, FW_IO_STDIO, true);
                  Log(LOG_DEBUG, "ws.media", "Started fwdsp %s.tx at %p", cptr->codec_tx,
                     codec_tx_subproc);
               } else if (strcasecmp(media_channel, "rx") == 0) {
                  if (cptr->codec_rx[0] != '\0') {
                     // XXX: Decrease refcnt on old codec
                  }
                  memset( cptr->codec_rx, 0, sizeof(cptr->codec_rx) );
                  memcpy(cptr->codec_rx, media_codec, 4);
                  codec_rx_subproc = fwdsp_find_or_create(cptr->codec_rx, FW_IO_STDIO, false);
                  Log(LOG_DEBUG, "ws.media", "Started fwdsp %s.rx at %p", cptr->codec_rx,
                     codec_rx_subproc);
               } else if (strcasecmp(media_channel, "video-rx") == 0) {
                  // NYI
               } else if (strcasecmp(media_channel, "video-tx") == 0) {
                  // NYI
               } else {
                  Log(LOG_CRIT, "ws.media", "invalid channel '%s' for codec message from cptr:<%p>",
                     media_channel, cptr);
               }
            }
         } else {
            Log(LOG_DEBUG, "ws.media", "No codec in media.codec cmd");
         }
      }
#endif
   } else if (mg_json_get(msg_data, "$.pong", NULL) > 0) {
      result = ws_handle_pong(msg, c);
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      result = ws_handle_auth_msg(msg, c);
   }
cleanup:
   dict_free(d);

   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (!c || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got msg <%p> c <%p> data <%p>", msg, c,
         (msg ? msg->data.buf : NULL) );

      return true;
   }
#if     defined(HTTP_DEBUG_CRAZY) || defined(DEBUG_PROTO)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
#endif
   // Binary (audio, waterfall) frames
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      Log(LOG_CRAZY, "ws.binframe", "Binary frame: %li bytes", msg->data.len);
      ws_binframe_process_mg(c, msg->data.buf, msg->data.len);
   } else {
      // Text (mostly json) frames
      Log(LOG_CRAZY, "ws", "Text frame: %li bytes", msg->data.len);
      ws_txtframe_process(msg, c);
   }
   return false;
}

#endif // defined(USE_MONGOOSE)

bool ws_send_ping(http_client_t *cptr) {
   if (!cptr || !cptr->is_ws) {
      return true;
   }
   // XXX: Send a ping, so they'll have something to respond to, to acknowledge
   // life
   char resp_buf[HTTP_WS_MAX_MSG + 1];
   if (!cptr) {
      Log(LOG_DEBUG, "auth", "ws_send_ping for null cptr!");

      return true;
   }
#if     defined(USE_MONGOOSE)
   if (!cptr->conn) {
      Log( LOG_DEBUG, "auth", "ws_send_ping for cptr:<%p> has mg_conn:<%p> and is invalid", cptr,
         (cptr ? cptr->conn : NULL) );

      return true;
   }
#endif // defined(USE_MONGOOSE)

   // Make sure that timeout will happen if no response
   cptr->last_ping = now;
   cptr->ping_attempts++;
   // only bother making noise if the first attempt failed, send the first ping
   // to crazy level log
   if (cptr->ping_attempts > 1) {
      Log(LOG_DEBUG, "ping", "sending ping to user %s on cptr:<%p> with ts:[%li] attempt %d",
         cptr->chatname, cptr, now, cptr->ping_attempts);
   } else {
      Log(LOG_CRAZY, "ping", "sending ping to user %s on cptr:<%p> with ts:[%li] attempt %d",
         cptr->chatname, cptr, now, cptr->ping_attempts);
   }
   char ping_buf[64];
   snprintf(ping_buf, sizeof(ping_buf), "{\"ping\":{\"ts\":%li}}", now);
#if     defined(USE_MONGOOSE)
   struct mg_connection *c = cptr->conn;
   mg_ws_send(c, ping_buf, strlen(ping_buf), WEBSOCKET_OP_TEXT);
#endif // defined(USE_MONGOOSE)

   return false;
}

/////////
// Send an error message to the user
bool ws_send_error(http_client_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   char *escaped_msg = escape_html(fullmsg);
   const char *jp = dict2json_mkstr(VAL_STR, "error.msg", escaped_msg, VAL_ULONG, "error.ts", now);

#if     defined(USE_MONGOOSE)
   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif // defined(USE_MONGOOSE)

   free(escaped_msg);
   free( (char *)jp );

   va_end(ap);

   return false;
}

// Send an alert message to the user
bool ws_send_alert(http_client_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   char *escaped_msg = escape_html(fullmsg);

   const char *jp = dict2json_mkstr(VAL_STR, "alert.msg", escaped_msg, VAL_ULONG, "alert.ts", now);

#if     defined(USE_MONGOOSE)
   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif // defined(USE_MONGOOSE)

   free(escaped_msg);
   free( (char *)jp );

   va_end(ap);

   return false;
}

#if     defined(USE_MONGOOSE)
bool ws_send_notice(struct mg_connection *c, const char *fmt, ...) {
   if (!c || !fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   va_end(ap);
   char *escaped_msg = escape_html(fullmsg);
   const char *jp = dict2json_mkstr(VAL_STR, "notice.msg", escaped_msg, VAL_ULONG, "notice.ts",
      now);

   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );
   free(escaped_msg);

   return false;
}

void ws_fini(struct mg_mgr *mgr) {
   mg_mgr_free(mgr);
}

#endif // defined(USE_MONGOOSE)

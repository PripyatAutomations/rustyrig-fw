// librrprotocol/rrclient.c
// rrcli helpers moved into librrprotocol and renamed to rrclient_*

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/rrclient.h>
#include <rrclient/ui.h>
extern struct mg_mgr mgr;
struct mg_connection *ws_conn = NULL;
bool ws_connected = false;
extern char session_token[HTTP_TOKEN_LEN + 1];
const char *login_user = NULL;

const char *get_server_property(const char *server, const char *prop) {
   if (!server || !prop) {
      return NULL;
   }
   char fullkey[1024];
   snprintf(fullkey, sizeof(fullkey), "server:%s.%s", server, prop);

   return cfg_get_exp(fullkey);
}

static void rrclient_ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   (void)c;

   if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

      if (msg && msg->data.buf) {
         char buf[HTTP_WS_MAX_MSG + 1];
         memset( buf, 0, sizeof(buf) );
         memcpy(buf, msg->data.buf, msg->data.len);
         dict *d = json2dict(buf);

         if (!d) {
            return;
         }
         char *cmd = dict_get(d, "talk.cmd", NULL);
         char *pong_ts = dict_get(d, "pong.ts", NULL);
         char *ping_ts = dict_get(d, "ping.ts", NULL);

         if (ping_ts) {
            const char *jp = dict2json_mkstr( VAL_STR, "type", "pong", VAL_ULONG, "ts", atol(ping_ts) );
            mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (void *)jp );
         } else if (pong_ts) {
            Log(LOG_CRAZY, "http.pong", "Received pong ts:%s", pong_ts);
         } else if (cmd && strcasecmp(cmd, "msg") == 0) {
            event_emit_dict("talk.msg", NULL, d);
         } else if (dict_get(d, "hello", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got hello from server");
         } else if (dict_get(d, "auth.cmd", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got auth message");
         }
         dict_free(d);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      ws_connected = true;
      event_emit("connected", NULL, NULL);

      login_user = cfg_get_exp("server.user");

      if (login_user) {
         const char *jp = dict2json_mkstr(VAL_STR, "hello", "rrcli");
         mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free( (void *)jp );

         jp = dict2json_mkstr(VAL_STR, "auth.cmd", "login", VAL_STR, "auth.user", login_user);
         mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free( (void *)jp );
      }
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      event_emit("goodbye", NULL, NULL);
   }
}

bool rrclient_connect(const char *url) {
   if (!url) {
      return true;
   }
   event_emit("connecting", NULL, NULL);
   ws_conn = mg_ws_connect(&mgr, url, rrclient_ws_handler, NULL, NULL);

   if (!ws_conn) {
      event_emit("http.error", NULL, NULL);

      return true;
   }

   return false;
}

bool rrclient_send_chat(const char *data) {
   if (!ws_conn || !data) {
      return true;
   }
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", data, VAL_STR, "talk.msg_type",
      "pub");
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (void *)jp );

   return false;
}

bool rrclient_send(const char *json) {
   if (!ws_conn || !json) {
      return true;
   }
   mg_ws_send(ws_conn, json, strlen(json), WEBSOCKET_OP_TEXT);

   return false;
}

bool rrclient_disconnect(void) {
   if (ws_conn) {
      ws_conn->is_closing = 1;
      ws_conn = NULL;
   }
   ws_connected = false;

   return false;
}

void rrclient_poll_events(void) {
   mg_mgr_poll(&mgr, 0);
}

bool rrclient_autoconnect(void) {
   const char *server = cfg_get_exp("server.auto-connect");

   if (server) {
      char server_name[256];
      snprintf(server_name, sizeof(server_name), "%s", server);
      free( (void *)server );

      char fullkey[1024];
      snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", server_name);
      const char *url = cfg_get_exp(fullkey);

      if (url) {
         rrclient_connect(url);
         free( (void *)url );
      }
   }

   return false;
}

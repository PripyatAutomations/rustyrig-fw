//
// rrcli/connman.c: Connection Manager for rrcli TUI client
//
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librustyaxe/logger.h>
#include <librrprotocol/rrprotocol.h>
#if defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif
#include <ev.h>

extern dict *cfg;
extern time_t now;
extern struct ev_loop *loop;
extern bool dying;
extern bool debug_sockets;

#if defined(USE_MONGOOSE)
struct mg_mgr mgr;
struct mg_connection *ws_conn = NULL;
bool ws_connected = false;
char session_token[HTTP_TOKEN_LEN+1] = {0};
const char *login_user = NULL;
const char *get_server_property(const char *server, const char *prop) {
    if (!server || !prop) {
       return NULL;
    }
    char fullkey[1024];
    snprintf(fullkey, sizeof(fullkey), "server:%s.%s", server, prop);
    return cfg_get_exp(fullkey);
}

static void rrcli_ws_handler(struct mg_connection *c, int ev, void *ev_data) {
    (void)c;
    if (ev == MG_EV_WS_MSG) {
       struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
       if (msg && msg->data.buf) {
          char buf[HTTP_WS_MAX_MSG+1];
          memset(buf, 0, sizeof(buf));
          memcpy(buf, msg->data.buf, msg->data.len);
          dict *d = json2dict(buf);
          if (!d) {
             return;
          }

          char *cmd = dict_get(d, "talk.cmd", NULL);
          char *pong_ts = dict_get(d, "pong.ts", NULL);
          char *ping_ts = dict_get(d, "ping.ts", NULL);

          if (ping_ts) {
             const char *jp = dict2json_mkstr(VAL_STR, "type", "pong", VAL_ULONG, "ts", atol(ping_ts));
             mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
             free((void *)jp);
          } else if (pong_ts) {
             Log(LOG_CRAZY, "http.pong", "Received pong ts:%s", pong_ts);
          } else if (cmd && strcasecmp(cmd, "msg") == 0) {
             char *from = dict_get(d, "talk.from", NULL);
             char *data = dict_get(d, "talk.data", NULL);
             char *msg_type = dict_get(d, "talk.msg_type", NULL);
             char *target = dict_get(d, "talk.target", NULL);
             time_t ts = dict_get_time_t(d, "talk.ts", now);

             if (from && data) {
                struct talk_msg_event_data {
                   char from[128];
                   char data[4096];
                   char target[128];
                   char msg_type[32];
                   time_t ts;
                } *tmed = calloc(1, sizeof(*tmed));
                if (tmed) {
                   snprintf(tmed->from, sizeof(tmed->from), "%s", from);
                   snprintf(tmed->data, sizeof(tmed->data), "%s", data);
                   snprintf(tmed->target, sizeof(tmed->target), "%s", target ? target : "");
                   snprintf(tmed->msg_type, sizeof(tmed->msg_type), "%s", msg_type ? msg_type : "pub");
                   tmed->ts = ts;
                   event_emit("talk.msg", NULL, tmed);
                   free(tmed);
                }
             }
          } else if (dict_get(d, "hello", NULL)) {
             Log(LOG_DEBUG, "ws", "Got hello from server");
          } else if (dict_get(d, "auth.cmd", NULL)) {
             Log(LOG_DEBUG, "ws", "Got auth message");
          }

          dict_free(d);
       }
    } else if (ev == MG_EV_WS_OPEN) {
       ws_connected = true;
       event_emit("http.connected", NULL, NULL);
       tui_print_win(tui_window_find("status"), "Connected to server");

       login_user = cfg_get_exp("server.user");
       if (login_user) {
          const char *jp = dict2json_mkstr(VAL_STR, "hello", "rrcli");
          mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
          free((void *)jp);

          jp = dict2json_mkstr(VAL_STR, "auth.cmd", "login", VAL_STR, "auth.user", login_user);
          mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
          free((void *)jp);
       }
    } else if (ev == MG_EV_CLOSE) {
       ws_connected = false;
       event_emit("http.disconnected", NULL, NULL);
       tui_print_win(tui_window_find("status"), "Disconnected from server");
    }
}

bool rrcli_connect(const char *url) {
    if (!url) {
       return true;
    }

    tui_print_win(tui_window_find("status"), "Connecting to %s", url);
    ws_conn = mg_ws_connect(&mgr, url, rrcli_ws_handler, NULL, NULL);

    if (!ws_conn) {
       tui_print_win(tui_window_find("status"), "Connection failed");
       return true;
    }
    return false;
}

bool rrcli_send_chat(const char *data) {
    if (!ws_conn || !data) {
       return true;
    }
    const char *jp = dict2json_mkstr(
       VAL_STR, "talk.cmd", "msg",
       VAL_STR, "talk.data", data,
       VAL_STR, "talk.msg_type", "pub");
    mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
    free((void *)jp);
    return false;
}

bool rrcli_send(const char *json) {
    if (!ws_conn || !json) {
       return true;
    }
    mg_ws_send(ws_conn, json, strlen(json), WEBSOCKET_OP_TEXT);
    return false;
}

bool rrcli_disconnect(void) {
    if (ws_conn) {
       ws_conn->is_closing = 1;
       ws_conn = NULL;
    }
    ws_connected = false;
    return false;
}

void rrcli_poll_events(void) {
    mg_mgr_poll(&mgr, 0);
}
#endif

bool rrcli_autoconnect(void) {
    const char *server = cfg_get_exp("server.auto-connect");
    if (server) {
       char server_name[256];
       snprintf(server_name, sizeof(server_name), "%s", server);
       free((void *)server);

       char fullkey[1024];
       snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", server_name);
       const char *url = cfg_get_exp(fullkey);
       if (url) {
          rrcli_connect(url);
          free((void *)url);
       }
    }
    return false;
}

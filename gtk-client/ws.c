#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"

extern dict *cfg;		// config.c
struct mg_mgr mgr;
bool ws_connected = false;
struct mg_connection *ws_conn = NULL;
// Deal with the binary requests
static bool ws_binframe_process(const char *buf, size_t len) {
   if (buf[0] == 'u') {  // PCM-u
   } else if (buf[0] == 'O') {
#if     defined(FEATURE_OPUS)
//       codec_decode_frame((unsigned char *)buf, len);
#endif                                                                                                  
    }                                                                                                   
    return false;                                                                                       
}

static bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   if (mg_json_get(msg_data, "$.cat.state", NULL) > 0) {
      char *vfo = mg_json_get_str(msg_data, "$.cat.state.vfo");
      double freq;
      mg_json_get_num(msg_data, "$.cat.state.freq", &freq);
      char *mode = mg_json_get_str(msg_data, "$.cat.state.mode");
      double width;
      mg_json_get_num(msg_data, "$.cat.state.width", &width);
      char *ptt = mg_json_get_str(msg_data, "$.cat.state.ptt");
      double power;
      mg_json_get_num(msg_data, "$.cat.state.power", &power);
      double ts;
      mg_json_get_num(msg_data, "$.cat.state.ts", &ts);
      char *user = mg_json_get_str(msg_data, "$.cat.state.user");
//      ui_print("State: VFO=%s freq=%.0f mode=%s width=%.0f ptt=%s (user=%s) power=%.0f ts=%.0f",
//          vfo, freq, mode, width, ptt, user, power, ts);

      // Update the display
      char freq_buf[24];
      memset(freq_buf, 0, sizeof(freq_buf));
      snprintf(freq_buf, sizeof(freq_buf), "%.3f", freq / 1000);
      gtk_entry_set_text(GTK_ENTRY(freq_entry), freq_buf);
      set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);

      // free memory
      free(vfo);
      free(mode);
      free(ptt);
      free(user);
   } else {
      ui_print(" ==> CAT: Unknown msg -- %s", msg_data);
   }
   return false;
}

static bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;

   if (c == NULL || msg == NULL) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg:<%x> mg_conn:<%x>", msg, c);
      return true;
   }

   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;
   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.ip, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.ip, ip, sizeof(ip));
   }

   if (msg->data.buf == NULL) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *nonce = mg_json_get_str(msg_data, "$.auth.nonce");
   char *user = mg_json_get_str(msg_data, "$.auth.user");
//   ui_print(" => cmd: '%s', nonce: %s, user: %s", cmd, nonce, user);

   // Must always send a command and username during auth
   if (cmd == NULL || (user == NULL)) {
      rv = true;
      goto cleanup;
   }

   if (strcasecmp(cmd, "challenge") == 0) {
      char *token = mg_json_get_str(msg_data, "$.auth.token");
      if (token != NULL) {
         session_token = strdup(token);
      } else {
         session_token = NULL;
      }
      ui_print("*** Sending PASSWD ***");
      ws_send_passwd(c, user, dict_get(cfg, "server.pass", NULL), nonce);
      free(token);
   } else if (strcasecmp(cmd, "authorized") == 0) {
      ui_print("*** Authorized ***");
   }

cleanup:
   free(cmd);
   free(nonce);
   free(user);
   return rv;
}

static bool ws_txtframe_process(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   bool result = false;


   if (mg_json_get(msg_data, "$.ping", NULL) > 0) {
      char ts_buf[32];
      double ping_ts = 0;
      mg_json_get_num(msg_data, "$.ping.ts", &ping_ts);

      if (ping_ts > 0) {
         memset(ts_buf, 0, sizeof(ts_buf));
         snprintf(ts_buf, sizeof(ts_buf), "%.0f", ping_ts);

         char pong[128];
         snprintf(pong, sizeof(pong), "{\"pong\": { \"ts\":\"%s\" } }", ts_buf);
         mg_ws_send(c, pong, strlen(pong), WEBSOCKET_OP_TEXT);
      }
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      result = ws_handle_auth_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.hello", NULL) > 0) {
      char *hello = mg_json_get_str(msg_data, "$.hello");
      ui_print("*** Server version: %s ***", hello);
      free(hello);
      goto cleanup;
   // Check for $.cat field (rigctl message)
   } else if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
//      ui_print("CAT msg");
      result = ws_handle_rigctl_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
//      ui_print("TALK msg");
//      if (cmd != NULL) {
//         result = ws_handle_chat_msg(msg, c);
//      }
   } else if (mg_json_get(msg_data, "$.pong", NULL) > 0) {
//      result = ws_handle_pong(msg, c);
   } else {
      ui_print("==> %s", msg->data);
   }
cleanup:
   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (c == NULL || msg == NULL || msg->data.buf == NULL) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg != NULL ? msg->data.buf : NULL));
      return true;
   }

#if     defined(HTTP_DEBUG_CRAZY)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
#endif

   // Binary (audio, waterfall) frames
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      ws_binframe_process(msg->data.buf, msg->data.len);
   } else {     // Text (mostly json) frames
      ws_txtframe_process(msg, c);
   }
   return false;
}

void ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_OPEN) {
//      c->is_hexdumping = 1;
      ws_conn = c; 
   } else if (ev == MG_EV_CONNECT) {
      const char *url = dict_get(cfg, "server.url", NULL);

      // XXX: Need to figure out how to deal with the certificate authority file?
      if (c->is_tls) {
         struct mg_tls_opts opts = { /*.ca = mg_unpacked("/certs/ca.pem"), */
                                 .name = mg_url_host(url)};
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      const char *login_user = dict_get(cfg, "server.user", NULL);
      ws_connected = true;
      update_connection_button(true, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-active");
      gtk_style_context_remove_class(ctx, "ptt-idle");

      if (login_user == NULL) {
         Log(LOG_CRIT, "core", "server.user not set in config!");
         exit(1);
      }
      ws_send_hello(c);
      ws_send_login(c, login_user);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      ws_handle(wm, c);
   } else if (ev == MG_EV_ERROR) {
      ui_print("Socket error: %s", (char *)ev_data);
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      ws_conn = NULL;
      update_connection_button(false, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-idle");
      gtk_style_context_remove_class(ctx, "ptt-active");
   }
}

void ws_init(void) {
//   mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   mg_mgr_init(&mgr);
}

void ws_fini(void) {
   mg_mgr_free(&mgr);
}

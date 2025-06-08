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
#include "inc/util.file.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
extern dict *cfg;		// config.c
struct mg_mgr mgr;
bool ws_connected = false;
struct mg_connection *ws_conn = NULL;
bool server_ptt_state = false;
const char *tls_ca_path = NULL;
struct mg_str tls_ca_path_str;

static bool sending_in_progress = false;

extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern const char *expand_path(const char *path); // main.c

const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

static bool ws_handle_talk_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   bool rv;

   char *cmd = mg_json_get_str(msg_data,    "$.talk.cmd");
   char *user = mg_json_get_str(msg_data,   "$.talk.user");
   char *privs = mg_json_get_str(msg_data,  "$.talk.privs");
   bool tx;
   mg_json_get_bool(msg_data, "$.talk.tx", &tx);
   char *muted = mg_json_get_str(msg_data,  "$.talk.muted");
   char *clones = mg_json_get_str(msg_data, "$.talk.clones");
   char *ts = mg_json_get_str(msg_data,     "$.talk.ts");

   if (!cmd) {
      rv = true;
      goto cleanup;
   }

   if (strcasecmp(cmd, "userinfo") == 0) {
      int clones_i = -1;
      if (clones != NULL) {
         clones_i = atoi(clones);
      }
      ui_print("UserInfo: %s -> %s (TX: %s, muted: %s, clones: %d", user, privs, (tx ? "true" : "false"), muted, clones_i);
   } else if (strcasecmp(cmd, "msg") == 0) {
      char *from = mg_json_get_str(msg_data, "$.talk.from");
      char *data = mg_json_get_str(msg_data, "$.talk.data");
      char *msg_type = mg_json_get_str(msg_data, "$.talk.msg_type");
      Log(LOG_DEBUG, "ws", "from: %s msg_type: %s data: |%s|", from, msg_type, data);

      if (strcasecmp(msg_type, "pub") == 0) {
         ui_print("[%s] <%s> %s", get_chat_ts(), from, data);
      } else if (strcasecmp(msg_type, "action") == 0) {
         ui_print("[%s] * %s %s", get_chat_ts(), from, data);
      }

      if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
         gtk_window_set_urgency_hint(GTK_WINDOW(main_window), TRUE);
      }
      free(data);
      free(from);
      free(msg_type);
   } else if (strcasecmp(cmd, "join") == 0) {
      char *ip = mg_json_get_str(msg_data, "$.talk.ip");
      if (user == NULL || ip == NULL) {
         free(ip);
         goto cleanup;
      }
      ui_print("[%s] >>> %s connected to the radio <<<", get_chat_ts(), user);
      free(ip);
   } else if (strcasecmp(cmd, "quit") == 0) {
      char *reason = mg_json_get_str(msg_data, "$.talk.reason");
      if (user == NULL || reason == NULL) {
         free(reason);
         goto cleanup;
      }
      ui_print("[%s] >> %s disconnected from the radio: %s <<<", get_chat_ts(), user, reason ? reason : "No reason given");
      free(reason);
   } else if (strcmp(cmd, "whois") == 0) {
      const char *json_array = mg_json_get_str(msg_data, "$.talk.data");
/*
      if (json_array) {
         ui_show_whois_dialog(GTK_WINDOW(main_window), json_array);
      }
*/
      ui_print(">>> WHOIS %s", user);
      ui_print("   %s", json_array);
   } else {
      Log(LOG_DEBUG, "chat", "msg: %.*s", msg->data.len, msg->data.buf);
   }

cleanup:
   free(cmd);
   free(user);
   free(privs);
   free(muted);
   free(clones);
   free(ts);
   return false;
}

static bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;

   if (mg_json_get(msg_data, "$.cat.state", NULL) > 0) {
      if (poll_block_expire < now) {
         char *vfo = mg_json_get_str(msg_data, "$.cat.state.vfo");
         double freq;
         mg_json_get_num(msg_data, "$.cat.state.freq", &freq);
         char *mode = mg_json_get_str(msg_data, "$.cat.state.mode");
         double width;
         mg_json_get_num(msg_data, "$.cat.state.width", &width);
         double power;
         mg_json_get_num(msg_data, "$.cat.state.power", &power);

         bool ptt = false;
         char *ptt_s = mg_json_get_str(msg_data, "$.cat.state.ptt");

         if (ptt_s != NULL && ptt_s[0] != '\0' && strcasecmp(ptt_s, "true") == 0) {
            ptt = true;
         }  else {
            ptt = false;
         }
         server_ptt_state = ptt;
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);

         double ts;
         mg_json_get_num(msg_data, "$.cat.ts", &ts);
         char *user = mg_json_get_str(msg_data, "$.cat.state.user");
         g_signal_handlers_block_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
         g_signal_handlers_unblock_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);

         // Update the display
         char freq_buf[24];
         memset(freq_buf, 0, sizeof(freq_buf));
         snprintf(freq_buf, sizeof(freq_buf), "%.3f", freq / 1000);

         if (freq_buf[0] != '\0' && strlen(freq_buf) > 0) {
            g_signal_handler_block(freq_entry, freq_changed_handler_id);
//            Log(LOG_DEBUG, "ws", "Updating freq_entry: %s", freq_buf);
            gtk_entry_set_text(GTK_ENTRY(freq_entry), freq_buf);
            g_signal_handler_unblock(freq_entry, freq_changed_handler_id);
         }

         if (mode && strlen(mode) > 0) {
//            Log(LOG_DEBUG, "ws", "Updating mode_combo: %s", mode);
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
         }

         // free memory
         free(vfo);
         free(mode);
         free(user);
//      } else {
//         Log(LOG_DEBUG, "ws.cat", "Polling blocked for %d seconds", (poll_block_expire - now));
      }
   } else {
//      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(), msg_data);
      Log(LOG_DEBUG, "ws.cat", "CAT msg: %s", msg->data);
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
//   ui_print("[%s] => cmd: '%s', nonce: %s, user: %s", get_chat_ts(), cmd, nonce, user);

   // Must always send a command and username during auth
   if (cmd == NULL || (user == NULL)) {
      rv = true;
      goto cleanup;
   }

   if (strcasecmp(cmd, "challenge") == 0) {
      char *token = mg_json_get_str(msg_data, "$.auth.token");

      if (token != NULL) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         ui_print("[%s] ?? Got CHALLENGE without valid token!", get_chat_ts());
         goto cleanup;
      }

      ui_print("[%s] *** Sending PASSWD ***", get_chat_ts());
      ws_send_passwd(c, user, dict_get(cfg, "server.pass", NULL), nonce);
      free(token);
   } else if (strcasecmp(cmd, "authorized") == 0) {
      ui_print("[%s] *** Authorized ***", get_chat_ts());
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
   } else if (mg_json_get(msg_data, "$.alert", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.alert.msg");
      char *from = mg_json_get_str(msg_data, "$.alert.from");

      if (msg != NULL) {
         ui_print("[%s] ALERT: %s !!!", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
      free(msg);
      free(from);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.error", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.error");
      if (msg != NULL) {
         ui_print("[%s] ERROR %s !!!", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.notice", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.notice");
      if (msg != NULL) {
         ui_print("[%s] NOTICE %s ***", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.hello", NULL) > 0) {
      char *hello = mg_json_get_str(msg_data, "$.hello");
      ui_print("[%s] *** Server version: %s ***", get_chat_ts(), hello);
      free(hello);
      goto cleanup;
   // Check for $.cat field (rigctl message)
   } else if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
//      ui_print("[%s] +++ CAT ++ %s", get_chat_ts(), msg_data);
      result = ws_handle_rigctl_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.talk", NULL) > 0) {
      result = ws_handle_talk_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.pong", NULL) > 0) {
//      result = ws_handle_pong(msg, c);
   } else if (mg_json_get(msg_data, "$.syslog", NULL) > 0) {
      char *ts = mg_json_get_str(msg_data, "$.syslog.ts");
      char *prio = mg_json_get_str(msg_data, "$.syslog.prio");
      char *subsys = mg_json_get_str(msg_data, "$.syslog.subsys");
      char *data = mg_json_get_str(msg_data, "$.syslog.data");
      char my_timestamp[64];
      time_t t;
      struct tm *tmp;
      memset(my_timestamp, 0, sizeof(my_timestamp));
      t = time(NULL);

      if ((tmp = localtime(&t)) != NULL) {
         /* success, proceed */
         if (strftime(my_timestamp, sizeof(my_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
            /* handle the error */
            memset(my_timestamp, 0, sizeof(my_timestamp));
            snprintf(my_timestamp, sizeof(my_timestamp), "<%lu>", time(NULL));
         }
      }

      log_print("[%s] <%s.%s> %s", my_timestamp, subsys, prio, data);
      free(ts);
      free(prio);
      free(subsys);
      free(data);
   } else {
      ui_print("[%s] ==> UnknownMsg: %.*s", get_chat_ts(), msg->data.len, msg->data.buf);
   }
cleanup:
   return false;
}

//      ws_binframe_process(msg->data.buf, msg->data.len);
bool ws_binframe_process(const char *data, size_t len) {
   if (data == NULL || len <= 10) {			// no real packet will EVER be under 10 bytes, even a keep-alive
      return true;
   }

//   if (data[0] == 'A' && data[1] == 'U') {
      audio_process_frame(data, len);
//   }
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

void http_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_OPEN) {
//      c->is_hexdumping = 1;
      ws_conn = c; 
   } else if (ev == MG_EV_CONNECT) {
      const char *url = dict_get(cfg, "server.url", NULL);

      if (c->is_tls) {
         struct mg_tls_opts opts = { .name = mg_url_host(url), .ca = tls_ca_path_str };
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_WRITE) {
      if (sending_in_progress) {
         free_sent_frame();
         sending_in_progress = false;
         try_send_next_frame(c);  // attempt to send the next
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
      ui_print("[%s] Socket error: %s", get_chat_ts(), (char *)ev_data);
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      ws_conn = NULL;
      update_connection_button(false, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-idle");
      gtk_style_context_remove_class(ctx, "ptt-active");
      ui_print("[%s] *** DISCONNECTED ***", get_chat_ts());
   }
}

void ws_init(void) {
   const char *debug = dict_get(cfg, "debug.http", "false");
   if (debug != NULL && (strcasecmp(debug, "true") == 0 ||
                        strcasecmp(debug, "yes") == 0)) {
      mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   }
   mg_mgr_init(&mgr);
   tls_ca_path = "*";
/*
   tls_ca_path = find_file_by_list(default_tls_ca_paths, sizeof(default_tls_ca_paths) / sizeof(char *));
*/
   if (tls_ca_path != NULL) {
      tls_ca_path_str = mg_str(tls_ca_path);
      Log(LOG_DEBUG, "ws", "Setting TLS CA path to %s", tls_ca_path);
   } else {
      Log(LOG_CRIT, "ws", "unable to find TLS CA file");
   }
}

void ws_fini(void) {
   mg_mgr_free(&mgr);
}

bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt) {
   if (c == NULL || vfo == NULL) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"ptt\", \"vfo\": \"%s\", \"ptt\": \"%s\" } }",
                 vfo, (ptt ? "true" : "false"));
//   Log(LOG_DEBUG, "CAT", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_ptt_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode) {
   if (c == NULL || vfo == NULL || mode == NULL) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"mode\", \"vfo\": \"%s\", \"mode\": \"%s\" } }",
                 vfo, mode);
   Log(LOG_DEBUG, "CAT", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_mode_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq) {
   if (c == NULL || vfo == NULL) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"freq\", \"vfo\": \"%s\", \"freq\": %.3f } }", vfo, freq);
   Log(LOG_DEBUG, "CAT", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_mode_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool connect_or_disconnect(GtkButton *button) {
   if (ws_connected) {
      ws_conn->is_closing = 1;
      ws_connected = false;
      gtk_button_set_label(button, "Connect");
      ws_conn = NULL;
   } else {
      const char *url = dict_get(cfg, "server.url", NULL);

      if (url) {
         // Connect to WebSocket server
         ws_conn = mg_ws_connect(&mgr, url, http_handler, NULL, NULL);
         if (strncasecmp(url, "wss://", 6) == 0) {
            struct mg_tls_opts opts;
            Log(LOG_DEBUG, "http", "tls_ca_path:<%x> %s, tls_ca_path_str:<%x>", tls_ca_path, tls_ca_path, tls_ca_path_str);
            opts.ca = tls_ca_path_str;
            mg_tls_init(ws_conn, &opts);
         }

         if (ws_conn == NULL) {
            ui_print("[%s] Socket connect error", get_chat_ts());
         }
         gtk_button_set_label(button, "Connecting...");
         ui_print("[%s] Connecting to %s", get_chat_ts(), url);
      }
   }
   return false;
}

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
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rustyrig/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
struct mg_mgr mgr;
bool ws_connected = false;	// Is RX stream connecte?
bool ws_tx_connected = false;	// Is TX stream connected?
struct mg_connection *ws_conn = NULL, *ws_tx_conn = NULL;
bool server_ptt_state = false;
const char *tls_ca_path = NULL;
struct mg_str tls_ca_path_str;

static bool sending_in_progress = false;
bool cfg_show_pings = true;			// set ui.show-pings=false in config to hide
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern dict *servers;
char active_server[512];

const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

static bool ws_handle_talk_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data,    "$.talk.cmd");
   char *user = mg_json_get_str(msg_data,   "$.talk.user");
   char *privs = mg_json_get_str(msg_data,  "$.talk.privs");
   char *muted = mg_json_get_str(msg_data,  "$.talk.muted");
   double clones;
   char *ts = mg_json_get_str(msg_data,     "$.talk.ts");
   bool rv = false;
   bool tx;
   mg_json_get_bool(msg_data, "$.talk.tx", &tx);
   mg_json_get_num(msg_data, "$.talk.clones", &clones);

   if (!cmd) {
      rv = true;
      goto cleanup;
   }


   if (cmd && strcasecmp(cmd, "userinfo") == 0) {
      if (!user) {
         rv = true;
         goto cleanup;
      }

      Log(LOG_DEBUG, "ws.talk", "[%s] UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %.0f)", get_chat_ts(), user, privs, (tx ? "true" : "false"), muted, clones);
      struct rr_user *cptr = find_or_create_client(user);

      if (cptr) {
         memset(cptr, 0, sizeof(struct rr_user));
         snprintf(cptr->name, sizeof(cptr->name), "%s", user);
         snprintf(cptr->privs, sizeof(cptr->privs), "%s", privs);
         if (tx) {
            cptr->is_ptt = true;
         }
         if (muted && strcasecmp(muted, "true") == 0) {
            cptr->is_muted = muted;
         }
         if (has_privs(cptr, "admin")) {
            set_flag(cptr, FLAG_ADMIN);
         } else if (has_privs(cptr, "owner")) {
            set_flag(cptr, FLAG_OWNER);
         }
         if (has_privs(cptr, "muted")) {
            set_flag(cptr, FLAG_MUTED);
            cptr->is_muted = true;
         }
         if (has_privs(cptr, "ptt")) {
            set_flag(cptr, FLAG_PTT);
         }
         if (has_privs(cptr, "subscriber")) {
            set_flag(cptr, FLAG_SUBSCRIBER);
         }
         if (has_privs(cptr, "elmer")) {
            set_flag(cptr, FLAG_ELMER);
         } else if (has_privs(cptr, "noob")) {
            set_flag(cptr, FLAG_NOOB);
         }
         if (has_privs(cptr, "bot")) {
            set_flag(cptr, FLAG_SERVERBOT);
         }
         if (has_privs(cptr, "listener")) {
            set_flag(cptr, FLAG_LISTENER);
         }
         if (has_privs(cptr, "syslog")) {
            set_flag(cptr, FLAG_SYSLOG);
         }
         if (has_privs(cptr, "tx")) {
            set_flag(cptr, FLAG_CAN_TX);
         }
         userlist_resync_all();
      } else {
         Log(LOG_CRIT, "ws", "OOM in ws_handle_talk_msg");
      }
   } else if (cmd && strcasecmp(cmd, "msg") == 0) {
      char *from = mg_json_get_str(msg_data, "$.talk.from");
      char *data = mg_json_get_str(msg_data, "$.talk.data");
      char *msg_type = mg_json_get_str(msg_data, "$.talk.msg_type");

      if (msg_type && strcasecmp(msg_type, "pub") == 0) {
         ui_print("[%s] <%s> %s", get_chat_ts(), from, data);
      } else if (msg_type && strcasecmp(msg_type, "action") == 0) {
         ui_print("[%s] * %s %s", get_chat_ts(), from, data);
      }

      if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
         gtk_window_set_urgency_hint(GTK_WINDOW(main_window), TRUE);
      }
      free(data);
      free(from);
      free(msg_type);
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      char *ip = mg_json_get_str(msg_data, "$.talk.ip");
      if (!user || !ip) {
         free(ip);
         goto cleanup;
      }
      
      struct rr_user *cptr = find_or_create_client(user);
      ui_print("[%s] >>> %s connected to the radio <<<", get_chat_ts(), user);
      Log(LOG_DEBUG, "ws.join", "New user %s has cptr:<%x>", user, cptr);
      free(ip);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = mg_json_get_str(msg_data, "$.talk.reason");
      if (!user || !reason) {
         free(reason);
         goto cleanup;
      }
      ui_print("[%s] >> %s disconnected from the radio: %s (%.0f clones left)<<<", get_chat_ts(), user, reason ? reason : "No reason given", --clones);

      struct rr_user *cptr = find_client(user);
      cptr->clones = clones;
      if (cptr->clones <= 0 ) {
         delete_client(cptr);
      }
      free(reason);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = mg_json_get_str(msg_data, "$.talk.data");
      ui_print("[%s] >>> WHOIS %s", user);
      ui_print("[%s]   %s", whois_msg);
   } else {
      Log(LOG_DEBUG, "chat", "msg: %.*s", msg->data.len, msg->data.buf);
   }

cleanup:
   free(cmd);
   free(user);
   free(privs);
   free(muted);
   free(ts);
   return false;
}

static bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;

   if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      if (poll_block_expire < now) {
         char *vfo = mg_json_get_str(msg_data, "$.cat.vfo");
         double freq;
         mg_json_get_num(msg_data, "$.cat.state.freq", &freq);
         char *mode = mg_json_get_str(msg_data, "$.cat.mode");
         double width;
         mg_json_get_num(msg_data, "$.cat.width", &width);
         double power;
         mg_json_get_num(msg_data, "$.cat.power", &power);

         bool ptt = false;
         char *ptt_s = mg_json_get_str(msg_data, "$.cat.ptt");

         if (ptt_s && ptt_s[0] != '\0' && strcasecmp(ptt_s, "true") == 0) {
            ptt = true;
         }  else {
            ptt = false;
         }
         server_ptt_state = ptt;
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);

         double ts;
         mg_json_get_num(msg_data, "$.cat.ts", &ts);
         char *user = mg_json_get_str(msg_data, "$.cat.user");
         g_signal_handlers_block_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
         g_signal_handlers_unblock_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);

         if (user) {
//            Log(LOG_DEBUG, "ws.cat", "user:<%x> = |%s|", user, user);
            struct rr_user *cptr = NULL;
            if ((cptr = find_client(user))) {
               Log(LOG_DEBUG, "ws.cat", "ptt set to %s for cptr:<%x>", (cptr->is_ptt ? "true" : "false"), cptr);
               cptr->is_ptt = ptt;
            }
         }


         if (freq > 0) {
//            g_signal_handler_block(freq_entry, freq_changed_handler_id);
            Log(LOG_CRAZY, "ws", "Updating freq_entry: %.0f", freq);
            gtk_freq_input_set_value(GTK_FREQ_INPUT(freq_entry), freq);
//            g_signal_handler_unblock(freq_entry, freq_changed_handler_id);
         }

         if (mode && strlen(mode) > 0) {
            Log(LOG_CRAZY, "ws", "Updating mode_combo: %s", mode);
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
         }

         // free memory
         free(vfo);
         free(mode);
         free(user);
      } else {
         Log(LOG_CRAZY, "ws.cat", "Polling blocked for %d seconds", (poll_block_expire - now));
      }
   } else {
      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(), msg_data);
      Log(LOG_DEBUG, "ws.cat", "Unknown msg: %s", msg->data);
   }
   return false;
}

static bool ws_handle_auth_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = false;

   if (!c || !msg) {
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

   if (!msg->data.buf) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from msg_conn:<%x> from %s:%d -- msg:<%x> with no data ptr", c, ip, port, msg);
      return true;
   }

   struct mg_str msg_data = msg->data;
   char *cmd = mg_json_get_str(msg_data, "$.auth.cmd");
   char *nonce = mg_json_get_str(msg_data, "$.auth.nonce");
   char *user = mg_json_get_str(msg_data, "$.auth.user");
//   ui_print("[%s] => cmd: '%s', nonce: %s, user: %s", get_chat_ts(), cmd, nonce, user);

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      char *token = mg_json_get_str(msg_data, "$.auth.token");

      if (token) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         ui_print("[%s] ?? Got CHALLENGE without valid token!", get_chat_ts());
         goto cleanup;
      }

      ui_print("[%s] *** Sending PASSWD ***", get_chat_ts());
      const char *login_pass = get_server_property(active_server, "server.pass");

      ws_send_passwd(c, user, login_pass, nonce);
      free(token);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
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
         snprintf(pong, sizeof(pong), "{ \"pong\": { \"ts\":\"%s\" } }", ts_buf);
         mg_ws_send(c, pong, strlen(pong), WEBSOCKET_OP_TEXT);
      }
      if (cfg_show_pings) {
         ui_print("[%s] * Ping? Pong! *", get_chat_ts());
      }
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.auth", NULL) > 0) {
      result = ws_handle_auth_msg(msg, c);
   } else if (mg_json_get(msg_data, "$.alert", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.alert.msg");
      char *from = mg_json_get_str(msg_data, "$.alert.from");

      if (msg) {
         ui_print("[%s] ALERT: %s !!!", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
      free(msg);
      free(from);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.error", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.error");
      if (msg) {
         ui_print("[%s] ERROR %s !!!", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.notice", NULL) > 0) {
      char *msg = mg_json_get_str(msg_data, "$.notice");
      if (msg) {
         ui_print("[%s] NOTICE %s ***", get_chat_ts(), msg);
      }
      free(msg);
      goto cleanup;
   } else if (mg_json_get(msg_data, "$.hello", NULL) > 0) {
      char *hello = mg_json_get_str(msg_data, "$.hello");
      char *codec = mg_json_get_str(msg_data, "$.codec");
      double rate;
      mg_json_get_num(msg_data, "$.rate", &rate);
      ui_print("[%s] *** Server version: %s with codec %s at %.1fKhz ***", get_chat_ts(), hello, codec, rate/1000);
      // XXX: Store the codec
      free(hello);
      free(codec);
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

      if ((tmp = localtime(&t))) {
         // success, proceed
         if (strftime(my_timestamp, sizeof(my_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
            // handle the error 
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
   if (!data || len <= 10) {			// no real packet will EVER be under 10 bytes, even a keep-alive
      return true;
   }

#if	defined(DEBUG_WS_BINFRAMES)
   char hex[128] = {0};
   size_t n = len < 16 ? len : 16;
   for (size_t i = 0; i < n; i++) {
      snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", (unsigned char)data[i]);
   }
   Log(LOG_DEBUG, "http.ws", "binary: %zu bytes, hex: %s", len, hex);
#endif

//   if (data[0] == 'A' && data[1] == 'U') {
      audio_process_frame(data, len);
//   }
   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (!c || !msg || !msg->data.buf) {
      Log(LOG_DEBUG, "http.ws", "ws_handle got msg <%x> c <%x> data <%x>", msg, c, (msg ? msg->data.buf : NULL));
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
#if	defined(HTTP_DEBUG_CRAZY)
      c->is_hexdumping = 1;
#endif
      ws_conn = c; 
   } else if (ev == MG_EV_CONNECT) {
      const char *url = get_server_property(active_server, "server.url");
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
      if (sending_in_progress) {
         Log(LOG_DEBUG, "ws", "http_handler: write frame");
         audio_tx_free_frame();
         sending_in_progress = false;
         try_send_next_frame(c);  // attempt to send the next
      }
   } else if (ev == MG_EV_WS_OPEN) {
      const char *login_user = get_server_property(active_server, "server.user");
      ws_connected = true;
      update_connection_button(true, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-active");
      gtk_style_context_remove_class(ctx, "ptt-idle");

      if (!login_user) {
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
      clear_client_list();
      ui_print("[%s] *** DISCONNECTED ***", get_chat_ts());
   }
}

void ws_init(void) {
   const char *debug = cfg_get("debug.http");
   if (debug && (strcasecmp(debug, "true") == 0 ||
                 strcasecmp(debug, "yes") == 0)) {
      mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   }
   mg_mgr_init(&mgr);

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

   const char *cfg_show_pings_s = cfg_get("ui.show-pings");

   if (cfg_show_pings_s && (strcasecmp(cfg_show_pings_s, "true") == 0 ||
      strcasecmp(cfg_show_pings_s, "yes") == 0)) {
      cfg_show_pings = true;
   } else {
      cfg_show_pings = false;
   }
   Log(LOG_DEBUG, "ws", "ws_init finished");
}

void ws_fini(void) {
   mg_mgr_free(&mgr);
}

bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt) {
   if (!c || !vfo) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"ptt\", \"vfo\": \"%s\", \"ptt\": \"%s\" } }",
                 vfo, (ptt ? "true" : "false"));

   Log(LOG_CRAZY, "CAT", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_ptt_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode) {
   if (!c || !vfo || !mode) {
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
   if (!c || !vfo) {
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

bool disconnect_server(void) {
   if (ws_connected) {
      if (ws_conn) {
         ws_conn->is_closing = 1;
      }
      ws_connected = false;
      gtk_button_set_label(GTK_BUTTON(conn_button), "Connect");
      ws_conn = NULL;
      clear_client_list();
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
      // Connect to WebSocket server
      ws_conn = mg_ws_connect(&mgr, url, http_handler, NULL, NULL);
      if (strncasecmp(url, "wss://", 6) == 0) {
         // XXX: any pre-init could go here but is generally handled above on detection of TLS
      }

      if (!ws_conn) {
         ui_print("[%s] Socket connect error", get_chat_ts());
      }
      gtk_button_set_label(GTK_BUTTON(conn_button), "Connecting...");
      ui_print("[%s] Connecting to %s", get_chat_ts(), url);
   } else {
      ui_print("[%s] * Server '%s' does not have a server.url configured! Check your config or maybe you mistyped it?", active_server);
   }

   return false;
}

bool connect_or_disconnect(GtkButton *button) {
   if (ws_connected) {
      disconnect_server();
   } else {
      connect_server();
   }
   return false;
}

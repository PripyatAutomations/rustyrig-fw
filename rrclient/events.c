//
// rrclient/events.c: event listeners for shared protocol events from
// librrprotocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>

extern const char *login_user;   // from connman.c
#ifdef	USE_GTK
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern GtkWidget *freq_entry, *log_view, *main_window, *ptt_button;
extern GtkTextBuffer *log_buffer;
#endif	// USE_GTK

extern int ws_connected;        // in librustyaxe/tui.window.c BUT belongs in rrclient!
bool cfg_ui_bell_chat = false;

static void rrclient_display_log_message(const char *msg) {
   if (!log_buffer || !msg) {
      return;
   }

   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      GtkTextIter end;
      gtk_text_buffer_get_end_iter(log_buffer, &end);
      gtk_text_buffer_insert(log_buffer, &end, msg, -1);
      gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
      g_idle_add(ui_scroll_to_end, log_view);
#endif	// USE_GTK
   }
}

void rrclient_update_connection_ui(int connected) {
   if (!conn_button) {
      return;
   }
   update_connection_button(connected, conn_button);

   // XXX: This should move to authenticated, so we show yellow 'til server has
   // approved us...
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      GtkStyleContext *ctx = gtk_widget_get_style_context( GTK_WIDGET(conn_button) );

      if (!ctx) {
         return;
      }

      if (connected == 1) {
         gtk_style_context_remove_class(ctx, "conn-idle");
         gtk_style_context_remove_class(ctx, "conn-pending");
         gtk_style_context_add_class(ctx, "conn-active");
      } else if (connected == 0) {
         gtk_style_context_remove_class(ctx, "conn-active");
         gtk_style_context_remove_class(ctx, "conn-pending");
         gtk_style_context_add_class(ctx, "conn-idle");
      } else if (connected == -1) {
         gtk_style_context_remove_class(ctx, "conn-active");
         gtk_style_context_remove_class(ctx, "conn-idle");
         gtk_style_context_add_class(ctx, "conn-pending");
      }
#endif	// USE_GTK
   }
}

static void rrclient_set_offline(void) {
   if (login_user) {
      free( (void *)login_user );
      login_user = NULL;
   }

   ws_connected = false;
#ifdef USE_MONGOOSE
   ws_conn = NULL;
#endif // USE_MONGOOSE
   rrclient_update_connection_ui(0);
   userlist_clear_all();
}

static void rrclient_handle_alert(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
//   fprintf(stderr, "[alert]\n");
//   dict_dump(d, stderr);

   time_t msg_ts = dict_get_time_t(d, "alert.ts", 0);
   const char *msg_from = dict_get(d, "alert.from", (char *)"*unknown*");
   const char *msg_data = dict_get(d, "alert.data", (char *)"*No message*");
   const char *msg_type = dict_get(d, "alert.type", (char *)"warning");

   // Print a colorized version of the test
   // XXX: Should we make a function to strip color escapes for below?
   ui_print(NULL, "{red}*** {bright-red}ALERT {red}***{reset} %s: %s", msg_from, msg_data);

   char my_msg[512];
   memset(my_msg, 0, sizeof(my_msg));
   snprintf(my_msg, sizeof(my_msg), "*** ALERT ***\nFrom: %s\nnMessage:\n\t%s", msg_from, msg_data);
   Log(LOG_INFO, "proto.alert", "*** ALERT From: %s --- ***", msg_from, msg_data);

   if (ui_mode == UI_MODE_GTK) {
      ui_message_bell();
      alert_dialog(GTK_WINDOW(main_window), MSG_ERROR, my_msg);
   }
   dict_free(d);
}

static void rrclient_handle_auth(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *a_cmd = dict_get(d, "auth.cmd", NULL);

   if (strcasecmp(a_cmd, "authorized") == 0) {
      time_t a_ts = dict_get_time_t(d, "auth.ts", now);
      const char *a_user = dict_get(d, "auth.user", NULL);
      const char *a_token = dict_get(d, "auth.token", NULL);
      const char *a_privs = dict_get(d, "auth.privs", NULL);

      ui_print( NULL,
         "%s {bright-cyan}Welcome back, {bright-yellow}%s{bright-cyan}! You have {bright-green}%s{bright-cyan} privileges",
         get_chat_ts(a_ts), a_user, a_privs);
   }
   dict_free(d);
}

static void rrclient_handle_autherr(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
//   fprintf(stderr, "[auth.error]\n");
//   dict_dump(d, stderr);
/*
 *  const char *from = dict_get(d, "talk.from", NULL);
 *  time_t msg_ts = dict_get_time_t(d, "talk.ts", 0);
 *  const char *msg_type = dict_get(d, "talk.msg_type", NULL);
 *  const char *msg_data = dict_get(d, "talk.data", NULL);
 */
   dict_free(d);
}

static void rrclient_handle_cat(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   dict_dump(d, stderr);
   const char *cat_user = dict_get(d, "cat.user", NULL);
   time_t msg_ts = dict_get_time_t(d, "cat.ts", 0);
   const char *cat_mode = dict_get(d, "cat.state.mode", NULL);
   const char *cat_vfo = dict_get(d, "cat.state.vfo", NULL);
   bool active = dict_get_bool(d, "cat.state.ptt", false);

   int cat_freq = dict_get_int(d, "cat.state.freq", 0.0);
   int cat_width = dict_get_int(d, "cat.state.width", 0);
   int cat_power = dict_get_int(d, "cat.state.power", 0);

   // Set VFO cat_vfo to the values extracted
   vfo_set_dict(d);

   if (ptt_button) {
      if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), (int)active);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), active);
#endif	// USE_GTK
      }
   }

   // Update the VFO display
}

static void rrclient_handle_catcmd(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[cat.cmd]\n");
   dict_dump(d, stderr);
/*
 *  const char *from = dict_get(d, "talk.from", NULL);
 *  time_t msg_ts = dict_get_time_t(d, "talk.ts", 0);
 *  const char *msg_type = dict_get(d, "talk.msg_type", NULL);
 *  const char *msg_data = dict_get(d, "talk.data", NULL);
 */
   dict_free(d);
}
static void rrclient_handle_hello(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *m_hwver = dict_get(d, "hello.hwver", (char *)"misconfigured radio");
   const char *m_swver = dict_get(d, "hello.swver", (char *)"1.2.3.4");
   ui_print(NULL, "%s {bright-yellow}Your host is running {bright-green}%s{bright-yellow} on {bright-green}%s",
      get_chat_ts(0), m_swver, m_hwver);
   dict_free(d);
}

static void rrclient_handle_log(const char *event, const char *data, rrconn_t *cptr, void *user) {
   dict *d = json2dict(data);
   char logmsg[512];
   memset(logmsg, 0, sizeof(logmsg));

   if (d) {
      const char *log_from =   dict_get(d, "log.from", (char *)"*unknown*");
      const char *log_msg =    dict_get(d, "log.msg", (char *)"*empty*");
      const char *log_subsys = dict_get(d, "log.subsys", (char *)"*unknown*");
      const char *log_prio =   dict_get(d, "log.prio", (char *)"info");
      time_t log_ts =          dict_get_time_t(d, (char *)"log.ts", now);

      snprintf(logmsg, sizeof(logmsg), "[%s] <%s.%s> From %s: %s",
         get_chat_ts(log_ts), log_subsys, log_prio, log_from, log_msg);
      rrclient_display_log_message(logmsg);
   }
}

static void rrclient_handle_talk_msg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *from = dict_get(d, "talk.from", NULL);
   time_t msg_ts = dict_get_time_t(d, "talk.ts", 0);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);
   const char *msg_data = dict_get(d, "talk.data", NULL);

   if (strcasecmp(msg_type, "action") == 0) {
      ui_print(NULL, "%s * %s %s", get_chat_ts(msg_ts), from, msg_data);
   } else {
      if (login_user != NULL && strcmp(from, login_user) == 0) {
         ui_print(NULL, "%s {yellow}=>{reset} %s", get_chat_ts(msg_ts), msg_data);
      } else {
         ui_print(NULL, "%s {yellow}<{reset}%s{yellow}>{reset} %s", get_chat_ts(msg_ts), from, msg_data);
      }
   }
   cfg_ui_bell_chat = cfg_get_bool("ui.bell.chat", false);

   if (cfg_ui_bell_chat) {
      ui_message_bell();
   }
   dict_free(d);
}

void tui_refresh_online_status(void) {
   tui_window_t *tw = tui_active_window();
   char connected_status[128];
   memset( connected_status, 0, sizeof(connected_status) );

   if (ws_connected == 1) {
      snprintf(connected_status, sizeof(connected_status), "{bright-green}ONLINE{reset}");
   } else if (ws_connected == 0) {
      snprintf(connected_status, sizeof(connected_status), "{bright-red}OFFLINE{reset}");
   } else if (ws_connected == -1) {
      snprintf(connected_status, sizeof(connected_status), "{bright-yellow}trying{reset}");
   }

   const char *win_color = "{bright-cyan}";

   if (tw->title[0] == '&' || tw->title[0] == '#') {
      win_color = "{bright-magenta}";
   }

   tui_update_status(tw, "{bright-black}[%s{bright-black}] [%s%s{bright-black}]{reset}", connected_status,
      win_color, tw->title);
}

static void rrclient_handle_connection(const char *event, const char *data, rrconn_t *cptr, void *user) {
#ifdef  USE_GTK
   GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
#endif // USE_GTK

   if (strcasecmp(event, "connecting") == 0) {
      ui_print( NULL, "%s *** {bright-yellow}Connecting{reset} ***", get_chat_ts(now) );
      rrclient_update_connection_ui(-1);
   } else if (strcasecmp(event, "connected") == 0) {
      if (!data) {
         return;
      }

      dict *d = json2dict(data);
      const char *user = dict_get(d, "auth.user", NULL);

      if (user) {
         // XXX: multiserver bug, discard old value if saved so we can store new without a
         // leak
         if (login_user) {
            free( (void *)login_user );
         }
         login_user = strdup(user);
         ui_print( NULL, "%s *** {green}Connected, logging in as %s{reset} ***", get_chat_ts(now), login_user );
      }
      rrclient_update_connection_ui(-1);
      dict_free(d);
   } else if (strcasecmp(event, "authorized") == 0) {
      ui_print( NULL, "%s **** {green}Logged in!", get_chat_ts(now) );
      rrclient_update_connection_ui(1);
   } else if (strcasecmp(event, "disconnect") == 0 || strcasecmp(event, "disconnected") == 0) {
      ui_print( NULL, "%s *** {red}DISCONNECTED{reset} ***", get_chat_ts(now) );
      rrclient_set_offline();
   } else if (strcasecmp(event, "http.error") == 0 || strcasecmp(event, "error") == 0) {
      ui_print(NULL, "{red}* http error *{reset}");
      rrclient_set_offline();
   }

   if (ui_mode == UI_MODE_TUI) {
      tui_refresh_online_status();
   }

}

static void rrclient_handle_freq(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);
//   fprintf(stderr, "[freq]\n");
//   dict_dump(d, stderr);
   long freq = dict_get_long(d, "cat.state.freq", 0);

   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      GtkWidget *entry = freq_entry;
      GtkFreqEntry *fe = GTK_FREQ_ENTRY(entry);

      if ( !gtk_freq_entry_is_editing(fe) ) {
         gtk_freq_entry_set_frequency(fe, freq);
      }
#endif // defined(USE_GTK)
   }
   dict_free(d);
}

static void rrclient_handle_join(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *m_user = dict_get(d, "talk.user", NULL);
   const char *m_ip = dict_get(d, "talk.ip", NULL);
   const char *m_target = dict_get(d, "talk.target", NULL);
   time_t m_ts = dict_get_time_t(d, "talk.ts", now);
   const char *s_unknown = "<UNKNOWN>";

   ui_print(NULL, "%s * %s (%s) joined %s", get_chat_ts(m_ts), m_user, m_ip, m_target);

   if ( !userlist_add_or_update(d) ) {
      Log(LOG_CRIT, "rrclient.events", "OOM in userlist_add_or_update");
   }

   // need to fire update uer list!
   dict_free(d);
}

static void rrclient_handle_mode(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data || !mode_combo) {
      return;
   }
   const char *mode = (const char *)data;

   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
#endif	// USE_GTK
   }
}

static void rrclient_handle_nomatch(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
//   fprintf(stderr, "[NOMATCH]\n");
//   dict_dump(d, stderr);
   dict_free(d);
}

static void rrclient_handle_ping(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   time_t p_ts = dict_get_time_t(d, "ping.ts", 0);
   dict_add(d, "cmd", "pong");
   dict_add_ulong(d, "pong.ts", p_ts);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}


static void rrclient_handle_quit(const char *event, const char *data, rrconn_t *cptr, void *user) {
//   fprintf(stderr, "[talk.quit]\n");

   if (!data) {
//      fprintf(stderr, "no talk.quit data\n");

      return;
   }

   dict *d = json2dict(data);
//   fprintf(stderr, "[quit]\n");
//   dict_dump(d, stderr);
   const char *masked_ip = "ip.hidden";
   const char *m_user = dict_get(d, "talk.user", NULL);
   const char *m_ip = dict_get(d, "talk.ip", (char *)masked_ip);
   const char *m_reason = dict_get(d, "talk.reason", NULL);
   // XXX: This needs to be fixed to add some awareness in the server of multiple channels
   const char *m_target = "&localrig";

   time_t m_ts = dict_get_time_t(d, "talk.ts", now);
   const char *s_unknown = "<UNKNOWN>";

   if (!m_user) {
      return;
   }

   ui_print(NULL, "%s * %s (%s) quit from %s: %s", get_chat_ts(m_ts), m_user, m_ip, m_target, m_reason);

   userlist_remove_by_name(m_user);
}

static void rrclient_handle_userinfo(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if ( !userlist_add_or_update(d) ) {
      Log(LOG_CRIT, "rrclient.events", "OOM in userlist_add_or_update");
   }
   dict_free(d);
}

static void rrclient_handle_whois(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   const char *whois_msg = (const char *)data;

   if (whois_msg && main_window) {

      if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
         ui_show_whois_dialog(GTK_WINDOW(main_window), whois_msg);
#endif	// USE_GTK
      }
   }
}

/*
 * Initialize the events we care about receiving
 */
void rrclient_register_events(void) {
   event_on("NOMATCH", rrclient_handle_nomatch, NULL);
   event_on("ws.msg.hello", rrclient_handle_hello, NULL);
   event_on("ws.msg.auth", rrclient_handle_auth, NULL);
   event_on("ws.msg.ping", rrclient_handle_ping, NULL);

   // Connection status related
   event_on("auth.error", rrclient_handle_autherr, NULL);
   event_on("authorized", rrclient_handle_connection, NULL);
   event_on("connected", rrclient_handle_connection, NULL);
   event_on("connecting", rrclient_handle_connection, NULL);
   event_on("disconnected", rrclient_handle_connection, NULL);

   // Status events
   event_on("alert", rrclient_handle_alert, NULL);
   event_on("error", rrclient_handle_connection, NULL);
   event_on("http.error", rrclient_handle_connection, NULL);

   // Chat/userlist related
   event_on("join", rrclient_handle_join, NULL);
   event_on("privmsg", rrclient_handle_talk_msg, NULL);
   event_on("quit", rrclient_handle_quit, NULL);
   event_on("talk.msg", rrclient_handle_talk_msg, NULL);
   event_on("userinfo", rrclient_handle_userinfo, NULL);
   event_on("whois", rrclient_handle_whois, NULL);

   // Log events
   event_on("log", rrclient_handle_log, NULL);

   // rigctl/CAT controls
   event_on("cat.cmd", rrclient_handle_catcmd, NULL);
   event_on("ws.msg.cat", rrclient_handle_cat, NULL);
}

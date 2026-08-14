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
#if defined(USE_GTK)
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern GtkWidget *main_window;
extern GtkTextBuffer *log_buffer;
extern GtkWidget *log_view;
#endif

static void rrclient_display_log_message(const char *msg) {
   if (!log_buffer || !msg) {
      return;
   }

   if (ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
      GtkTextIter end;
      gtk_text_buffer_get_end_iter(log_buffer, &end);
      gtk_text_buffer_insert(log_buffer, &end, msg, -1);
      gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
      g_idle_add(ui_scroll_to_end, log_view);
#endif
   }
}

static void rrclient_update_connection_ui(bool connected) {

   if (!conn_button) {
      return;
   }
   update_connection_button(connected, conn_button);

   // XXX: This should move to authenticated, so we show yellow 'til server has
   // approved us...
   if (ui_mode == UI_MODE_GTK) {
#if defined(USE_GTK)
      GtkStyleContext *ctx = gtk_widget_get_style_context( GTK_WIDGET(conn_button) );

      if (!ctx) {
         return;
      }

      if (connected) {
         gtk_style_context_remove_class(ctx, "conn-idle");
         gtk_style_context_remove_class(ctx, "conn-pending");
         gtk_style_context_add_class(ctx, "conn-active");
      } else {
         gtk_style_context_remove_class(ctx, "conn-active");
         gtk_style_context_remove_class(ctx, "conn-pending");
         gtk_style_context_add_class(ctx, "conn-idle");
      }
#endif
   }
}

static void rrclient_handle_connection_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (strcasecmp(event, "connecting") == 0) {
      ui_print( NULL, "%s *** {bright-yellow}Connecting{reset} ***", get_chat_ts(now) );
   } else if (strcasecmp(event, "connected") == 0) {
      dict *d = json2dict(data);
      const char *user = dict_get(d, "auth.user", NULL);

      if (login_user) {
         free( (void *)login_user );
      }
      login_user = strdup(user);
      rrclient_update_connection_ui(true);
      ui_print( NULL, "%s *** {green}Connected{reset} ***", get_chat_ts(now) );
      dict_free(d);
   } else if (strcasecmp(event, "goodbye") == 0 || strcasecmp(event, "disconnect") == 0) {
      if (login_user) {
         free( (void *)login_user );
         login_user = NULL;
      }
      rrclient_update_connection_ui(false);
      ui_print( NULL, "%s *** {red}DISCONNECTED{reset} ***", get_chat_ts(now) );
      ws_connected = false;
      ws_conn = NULL;
      update_connection_button(false, conn_button);
      userlist_clear_all();
   } else if (strcasecmp(event, "http.error") == 0 || strcasecmp(event, "error") == 0) {
      ui_print(NULL, "{red}* http error *{reset}");
      rrclient_update_connection_ui(false);
   }
}

static void rrclient_handle_ptt_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *cat_user = dict_get(d, "cat.user", NULL);
   time_t msg_ts = dict_get_time_t(d, "cat.ts", 0);
   const char *cat_mode = dict_get(d, "cat.state.mode", NULL);
   const char *cat_vfo = dict_get(d, "cat.state.vfo", NULL);
   bool active = dict_get_bool(d, "cat.state.ptt", false);

   int cat_freq = dict_get_int(d, "cat.state.freq", 0.0);
   int cat_width = dict_get_int(d, "cat.state.width", 0);
   int cat_power = dict_get_int(d, "cat.state.power", 0);

   fprintf(stderr, "[rig.ptt]\n");
   dict_dump(d, stderr);

   if (ptt_button) {

      if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), active);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), active);
#endif // defined(USE_GTK)
      }
   }
}

static void rrclient_handle_freq_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);
   fprintf(stderr, "[freq]\n");
   dict_dump(d, stderr);
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

static void rrclient_handle_mode_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data || !mode_combo) {
      return;
   }
   const char *mode = (const char *)data;

   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
#endif // defined(USE_GTK)
   }
}

static void rrclient_handle_userinfo_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if ( !userlist_add_or_update(d) ) {
      Log(LOG_CRIT, "rrclient.events", "OOM in userlist_add_or_update");
   }
   dict_free(d);
}

static void rrclient_handle_join_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[talk.join]\n");
   dict_dump(d, stderr);
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

static void rrclient_handle_quit_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   fprintf(stderr, "[talk.quit]\n");
   if (!data) {
      fprintf(stderr, "no talk.quit data\n");
      return;
   }

   dict *d = json2dict(data);
   dict_dump(d, stderr);
   const char *masked_ip = "ip.hidden";
   const char *m_user = dict_get(d, "talk.user", NULL);
   const char *m_ip = dict_get(d, "talk.ip", (char *)masked_ip);
   const char *m_reason = dict_get(d, "talk.reason", NULL);

   time_t m_ts = dict_get_time_t(d, "talk.ts", now);
   const char *s_unknown = "<UNKNOWN>";

   if (!m_user) {
      return;
   }
   ui_print(NULL, "%s * %s (%s) quit from %s", get_chat_ts(m_ts), m_user, m_ip, m_reason);

   userlist_remove_by_name(m_user);
}

static void rrclient_handle_whois_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   const char *whois_msg = (const char *)data;

   if (whois_msg && main_window) {

      if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
         ui_show_whois_dialog(GTK_WINDOW(main_window), whois_msg);
#endif
      }
   }
}

static void rrclient_handle_log_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   struct log_event_data *led = (struct log_event_data *)data;

   if (!led || !led->message[0]) {
      return;
   }
   rrclient_display_log_message(led->message);
}

static void rrclient_handle_talk_msg_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
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
   dict_free(d);
}

static void rrclient_handle_alert_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   dict_dump(d, stderr);
   fprintf(stderr, "[alert]\n");

   const char *from = dict_get(d, "talk.from", NULL);
   time_t msg_ts = dict_get_time_t(d, "alert.ts", 0);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);
   const char *msg_data = dict_get(d, "alert.data", NULL);

   dict_free(d);
}

static void rrclient_handle_autherr_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[auth.error]\n");
   dict_dump(d, stderr);
/*
 *  const char *from = dict_get(d, "talk.from", NULL);
 *  time_t msg_ts = dict_get_time_t(d, "talk.ts", 0);
 *  const char *msg_type = dict_get(d, "talk.msg_type", NULL);
 *  const char *msg_data = dict_get(d, "talk.data", NULL);
 */
   dict_free(d);
}

static void rrclient_handle_catcmd_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
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

/*
 * Initialize the events we care about receiving
 */
void rrclient_register_events(void) {
   // Connection status related
   event_on("connected", rrclient_handle_connection_event, NULL);
   event_on("connecting", rrclient_handle_connection_event, NULL);
   event_on("disconnected", rrclient_handle_connection_event, NULL);
   event_on("auth.error", rrclient_handle_autherr_event, NULL);

   // Status events
   event_on("alert", rrclient_handle_alert_event, NULL);
   event_on("error", rrclient_handle_connection_event, NULL);
   event_on("http.error", rrclient_handle_connection_event, NULL);

   // Chat/userlist related
   event_on("join", rrclient_handle_join_event, NULL);
   event_on("privmsg", rrclient_handle_talk_msg_event, NULL);
   event_on("quit", rrclient_handle_quit_event, NULL);
   event_on("talk.msg", rrclient_handle_talk_msg_event, NULL);
   event_on("userinfo", rrclient_handle_userinfo_event, NULL);
   event_on("whois", rrclient_handle_whois_event, NULL);

   // Log events
   event_on("log", rrclient_handle_log_event, NULL);

   // CAT controls
   event_on("cat.cmd", rrclient_handle_catcmd_event, NULL);
   event_on("rig.ptt", rrclient_handle_ptt_event, NULL);
   event_on("rig.freq", rrclient_handle_freq_event, NULL);
   event_on("rig.mode", rrclient_handle_mode_event, NULL);
}

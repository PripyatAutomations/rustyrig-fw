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
//
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>

extern const char *login_user;   // from connman.c
#ifdef	USE_GTK
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern int cfg_ui_ptt_ack_timeout;   // gtk.ptt-btn.c
extern void ptt_button_tot_expired(void);   // gtk.ptt-btn.c
extern GtkWidget *freq_entry, *log_view, *main_window, *ptt_button;
#endif	// USE_GTK

extern int ws_connected;        // in librustyaxe/tui.window.c BUT belongs in rrclient!
bool cfg_ui_bell_chat = false;

void rrclient_update_connection_ui(int connected) {
   if (!conn_button) {
      return;
   }

   // XXX: This should move to authenticated, so we show yellow 'til server has
   // approved us...
   if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      update_connection_button(connected, conn_button);
#endif	// USE_GTK
   } else if (ui_mode == UI_MODE_TUI) {
     // Set the status line contents
   }
}

char sb_online[128];	// due to formatting (24 char real)
char sb_window[128];	// due to formatting (16 char real)
char sb_vfo[32];
const char *current_vfo = "A";

// Refresh status bar online status section
void tui_refresh_sb_online(void) {
   memset( sb_online, 0, sizeof(sb_online) );

   if (ws_connected == 1) {
      snprintf(sb_online, sizeof(sb_online), "{bright-black}[{bright-green}ONLINE{bright-black}]{reset}");
   } else if (ws_connected == 0) {
      snprintf(sb_online, sizeof(sb_online), "{bright-black}[{bright-red}OFFLINE{bright-black}]{reset}");
   } else if (ws_connected == -1) {
      snprintf(sb_online, sizeof(sb_online), "{bright-black}[{bright-yellow}Trying{bright-black}]{reset}");
   }
}

// Refresh statusbar window name section
void tui_refresh_sb_window(void) {
   tui_window_t *tw = tui_active_window();
   memset( sb_window, 0, sizeof(sb_window));
   const char *win_color = "{bright-cyan}";
   if (tw->title[0] == '&' || tw->title[0] == '#') {
      win_color = "{bright-magenta}";
   }
   snprintf(sb_window, sizeof(sb_window),
    "{bright-black}[%s%s{bright-black}]{reset}",
     win_color, tw->title);
}

// Refresh statusbar VFO section
// Reads the central VFO state saved by vfo_set_dict() - the same copy
// the GTK UI uses.  Shows the ACTIVE VFO (single upper case letter).
// Never hardcode values here.
void tui_refresh_sb_vfo(void) {
   memset(sb_vfo, 0, sizeof(sb_vfo));

   char vfo_str[2] = { vfo_state_get_active(), 0 };
   const char *vfo = vfo_state_get(vfo_str, "cat.state.vfo", vfo_str);
   long freq = vfo_state_get_long(vfo_str, "cat.state.freq", 0);
   const char *mode = vfo_state_get(vfo_str, "cat.state.mode", "---");
   long width = vfo_state_get_long(vfo_str, "cat.state.width", 0);
   // Show freq in kHz with hz precision: 7200000 -> 7200.000
   snprintf(sb_vfo, sizeof(sb_vfo), "<VFO %s: %.3f/%s-%ld>", vfo, freq / 1000.0, mode, width);
}

static void rrclient_set_offline(void) {
   if (login_user) {
      free( (void *)login_user );
      login_user = NULL;
   }

   rrclient_update_connection_ui(0);
   userlist_clear_all();

#ifdef	USE_GTK
   // PTT button goes back to dark grey while offline
   ptt_button_set_online(false);
#endif

   if (!ws_conn) {
      return;
   }

   ws_connected = false;
   ws_conn->conn = NULL;
   free(ws_conn);
   ws_conn = NULL;
}

static void rrclient_handle_alert(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   time_t msg_ts = dict_get_time_t(d, "msg.ts", 0);
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

// Server TX timed out (TOT) - flag it on the PTT button (orange) via the
// dedicated ptt.tot-expired message from the server
static void rrclient_handle_ptt_tot(const char *event, const char *data, rrconn_t *cptr, void *user) {
#ifdef	USE_GTK
   ptt_button_tot_expired();
#endif
}

static void rrclient_handle_auth(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *a_cmd = dict_get(d, "auth.cmd", NULL);

   if (strcasecmp(a_cmd, "authorized") == 0) {
      time_t a_ts = dict_get_time_t(d, "msg.ts", now);
      const char *a_user = dict_get(d, "auth.user", NULL);
      const char *a_token = dict_get(d, "auth.token", NULL);
      const char *a_privs = dict_get(d, "auth.privs", NULL);

      ui_print( NULL, "%s {bright-cyan}Welcome back, {bright-yellow}%s{bright-cyan}! You have {bright-green}%s{bright-cyan} privileges",
         get_chat_ts(a_ts), a_user, a_privs);
      if (ui_mode == UI_MODE_TUI) {
         tui_refresh_sb_online();
      }
   }
   dict_free(d);
}

static void rrclient_handle_autherr(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *error_msg = dict_get(d, "error.msg", NULL);
   Log(LOG_INFO, "ws.auth", "AUTHENTICATION ERROR: %s", error_msg);
   dict_dump(d, NULL);
   dict_free(d);
}

static void rrclient_handle_cat(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   // vfo_set_dict() saves all cat.* keys into the central VFO state, namespaced
   // by the VFO letter from the dict (cat.state.vfo), and pushes the update for
   // the active VFO to the UI (GTK widgets or TUI statusbar).
   // All UIs read from that saved state - never from the event dict.
   vfo_set_dict(NULL, d);
   dict_free(d);
}

static void rrclient_handle_catcmd(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
/*
 *  const char *from = dict_get(d, "talk.from", NULL);
 *  time_t msg_ts = dict_get_time_t(d, "msg.ts", 0);
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
      time_t log_ts =          dict_get_time_t(d, (char *)"msg.ts", now);

      snprintf(logmsg, sizeof(logmsg), "[%s] <%s.%s> From %s: %s",
         get_chat_ts(log_ts), log_subsys, log_prio, log_from, log_msg);
   }
}

static void rrclient_handle_talk_msg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *from = dict_get(d, "talk.from", NULL);
   time_t msg_ts = dict_get_time_t(d, "msg.ts", 0);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);
   const char *msg_data = dict_get(d, "talk.data", NULL);
   const char *msg_cmd = dict_get(d, "talk.cmd", NULL);

   if (msg_cmd && strcasecmp(msg_cmd, "replay-start") == 0) {
      ui_print(NULL, "{red}>>>{reset} Start of chat chat replay. {red}<<<{reset}");
   } else if (msg_cmd && strcasecmp(msg_cmd, "replay-completed") == 0) {
      ui_print(NULL, "{red}>>>{reset} Finished chat replay. {red}<<<{reset}");
   } else {
      if (strcasecmp(msg_type, "action") == 0) {
         ui_print(NULL, "%s {yellow}*{reset} %s %s", get_chat_ts(msg_ts), from, msg_data);
      } else if (strcasecmp(msg_type, "pub") == 0) {
         if (login_user != NULL && strcmp(from, login_user) == 0) {
            ui_print(NULL, "%s {yellow}=>{reset} %s", get_chat_ts(msg_ts), msg_data);
         } else {
            ui_print(NULL, "%s {yellow}<{reset}%s{yellow}>{reset} %s", get_chat_ts(msg_ts), from, msg_data);
         }
      } else if (strcasecmp(msg_type, "replay-pub") == 0) {
        ui_print(NULL, "%s {red}<{reset}%s{red}>{reset} %s", get_chat_ts(msg_ts), from, msg_data);
      } else if (strcasecmp(msg_type, "replay-action") == 0) {
         ui_print(NULL, "%s {red}*{reset} %s %s", get_chat_ts(msg_ts), from, msg_data);
      } else if (strcasecmp(msg_type, "priv") == 0) {
         ui_print(NULL, "%s {bright-green}*{reset}%s{bright-green}*{reset} %s %s", get_chat_ts(msg_ts), from, msg_data);
      } else if (strcasecmp(msg_type, "replay-priv") == 0) {
         ui_print(NULL, "%s {magenta}*{reset}%s{magenta}*{reset} %s %s", get_chat_ts(msg_ts), from, msg_data);
      }
      cfg_ui_bell_chat = cfg_get_bool("ui.bell.chat", false);

      if (cfg_ui_bell_chat) {
         ui_message_bell();
      }
   }
   dict_free(d);
}

static void rrclient_handle_connection(const char *event, const char *data, rrconn_t *cptr, void *user) {
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
         // XXX: multiserver bug, discard old value if saved so we can store new without a leak
         if (login_user) {
            free( (void *)login_user );
         }
         login_user = strdup(user);
         ui_print( NULL, "%s *** {green}Connected, logging in as %s{reset} ***", get_chat_ts(now), login_user );
      }
      rrclient_update_connection_ui(-1);
#ifdef	USE_GTK
      ptt_button_set_online(true);   // button turns green once we're online
#endif
      dict_free(d);
   } else if (strcasecmp(event, "authorized") == 0) {
      ui_print( NULL, "%s *** {green}Logged in!{reset} ***", get_chat_ts(now) );
      rrclient_update_connection_ui(1);
#ifdef	USE_GTK
      ptt_button_set_online(true);
#endif
   } else if (strcasecmp(event, "disconnect") == 0 || strcasecmp(event, "disconnected") == 0) {
      ui_print( NULL, "%s *** {red}DISCONNECTED{reset} ***", get_chat_ts(now) );
      rrclient_set_offline();
   } else if (strcasecmp(event, "http.error") == 0 || strcasecmp(event, "error") == 0) {
      ui_print(NULL, "{red}* http error *{reset}");
      rrclient_set_offline();
   }

   if (ui_mode == UI_MODE_TUI) {
      tui_refresh_sb_online();
   }
}

static void rrclient_handle_freq(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);
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
   time_t m_ts = dict_get_time_t(d, "msg.ts", now);
   const char *s_unknown = "<UNKNOWN>";

   ui_print(NULL, "%s * %s (%s) joined %s", get_chat_ts(m_ts), m_user, m_ip, m_target);

   if ( !userlist_add_or_update(d) ) {
      Log(LOG_CRIT, "rrclient.events", "OOM in userlist_add_or_update");
   }

   // need to fire update user list!
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
   Log(LOG_CRAZY, "ws.nomatch", "Got NOMATCH hit with json: %s", data);
}

static void rrclient_handle_quit(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   const char *masked_ip = "ip.hidden";
   const char *m_user = dict_get(d, "talk.user", NULL);
   const char *m_ip = dict_get(d, "talk.ip", (char *)masked_ip);
   const char *m_reason = dict_get(d, "talk.reason", NULL);
   const char *m_target = "&localrig";

   time_t m_ts = dict_get_time_t(d, "msg.ts", now);
   const char *s_unknown = "<UNKNOWN>";

   if (!m_user) {
      dict_free(d);
      return;
   }

   ui_print(NULL, "%s * %s (%s) quit from %s: %s", get_chat_ts(m_ts), m_user, m_ip, m_target, m_reason);

   userlist_remove_by_name(m_user);
   dict_free(d);
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
   event_on("ws.talk.msg", rrclient_handle_talk_msg, NULL);
   event_on("userinfo", rrclient_handle_userinfo, NULL);
   event_on("whois", rrclient_handle_whois, NULL);

   // Log events
   event_on("log", rrclient_handle_log, NULL);

   // rigctl/CAT controls
   event_on("cat.cmd", rrclient_handle_catcmd, NULL);
   event_on("ws.msg.cat", rrclient_handle_cat, NULL);

   // Server TX timeout (TOT) fired
   event_on("ws.msg.ptt.tot-expired", rrclient_handle_ptt_tot, NULL);
}

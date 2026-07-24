//
// rrgtk/events.c: GTK-side event listeners for shared protocol events
//
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrgtk/connman.h>
#include <rrgtk/userlist.h>
#include <mod.ui.gtk3/gtk.core.h>
#include <librustyaxe/logger.h>

extern const char *login_user;	// from connman.c
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern GtkWidget *main_window;
extern GtkTextBuffer *log_buffer;
extern GtkWidget *log_view;

static void rrgtk_display_log_message(const char *msg) {
    if (!log_buffer || !msg) {
       return;
    }
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer, &end);
    gtk_text_buffer_insert(log_buffer, &end, msg, -1);
    gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
    g_idle_add(ui_scroll_to_end, log_view);
}

static void rrgtk_handle_connection_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)data;

   if (strcmp(event, "http.connected") == 0) {
      update_connection_button(true, conn_button);
      login_user = cfg_get_exp("server.user");
   } else if (strcmp(event, "http.disconnected") == 0) {
      update_connection_button(false, conn_button);
   } else if (strcmp(event, "http.error") == 0) {
      ui_print("{red}* http error *{reset}");
   }
}

static void rrgtk_handle_ptt_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data) {
      return;
   }

   bool active = *(bool *)data;
   if (ptt_button) {
      update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), active);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), active);
   }
}

static void rrgtk_handle_freq_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data || !freq_entry) {
      return;
   }

   double freq = *(double *)data;
   GtkWidget *entry = freq_entry;
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(entry);
   if (!gtk_freq_entry_is_editing(fe)) {
      gtk_freq_entry_set_frequency(fe, (unsigned long)freq);
   }
}

static void rrgtk_handle_mode_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data || !mode_combo) {
      return;
   }

   const char *mode = (const char *)data;
   set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
}

static void rrgtk_handle_userinfo_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data) {
      return;
   }

   struct rr_user *info = (struct rr_user *)data;
   if (!userlist_add_or_update(info)) {
      Log(LOG_CRIT, "rrgtk.events", "OOM in userlist_add_or_update");
   }
}

static void rrgtk_handle_userjoin_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data) {
      return;
   }

   struct rr_user *info = (struct rr_user *)data;
   if (!userlist_add_or_update(info)) {
      Log(LOG_CRIT, "rrgtk.events", "OOM in userlist_add_or_update");
   }
}

static void rrgtk_handle_userquit_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)event;
   if (!data) {
      return;
   }

   char *name = (char *)data;
   if (!name) {
      return;
   }

   userlist_remove_by_name(name);
}

static void rrgtk_handle_whois_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
    (void)event;
    (void)cptr;
    (void)user;
    if (!data) {
       return;
    }

    const char *whois_msg = (const char *)data;
    if (whois_msg && main_window) {
       ui_show_whois_dialog(GTK_WINDOW(main_window), whois_msg);
    }
}

static void rrgtk_handle_log_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
    (void)event;
    (void)cptr;
    (void)user;
    struct log_event_data *led = (struct log_event_data *)data;
    if (!led || !led->message[0]) {
       return;
    }
    rrgtk_display_log_message(led->message);
}

struct talk_msg_event_data {
    char from[128];
    char data[4096];
    char target[128];
    char msg_type[32];
    time_t ts;
};

static void rrgtk_handle_talk_msg_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
    (void)event;
    (void)cptr;
    (void)user;
    char *prefix = "";

    struct talk_msg_event_data *tmed = (struct talk_msg_event_data *)data;
    if (!tmed || !tmed->from[0] || !tmed->data[0]) {
       return;
    }

    if (login_user != NULL && tmed->from[0] != '\0' && strcmp(tmed->from, login_user) == 0) {
       prefix = "=> ";
    }

    if (strcasecmp(tmed->msg_type, "action") == 0) {
       ui_print("%s%s * %s %s", prefix, get_chat_ts(tmed->ts), tmed->from, tmed->data);
    } else {
       ui_print("%s%s {yellow}<{reset}%s{yellow}>{reset} %s", prefix, get_chat_ts(tmed->ts), tmed->from, tmed->data);
    }
}

void rrgtk_register_events(void) {
    event_on("http.connected", rrgtk_handle_connection_event, NULL);
    event_on("http.error", rrgtk_handle_connection_event, NULL);
    event_on("http.disconnected", rrgtk_handle_connection_event, NULL);
    event_on("http.rig.ptt", rrgtk_handle_ptt_event, NULL);
    event_on("http.rig.freq", rrgtk_handle_freq_event, NULL);
    event_on("http.rig.mode", rrgtk_handle_mode_event, NULL);
    event_on("http.userinfo", rrgtk_handle_userinfo_event, NULL);
    event_on("http.userjoin", rrgtk_handle_userjoin_event, NULL);
    event_on("http.userquit", rrgtk_handle_userquit_event, NULL);
    event_on("http.whois", rrgtk_handle_whois_event, NULL);
    event_on("log.message", rrgtk_handle_log_event, NULL);
    event_on("talk.msg", rrgtk_handle_talk_msg_event, NULL);
}

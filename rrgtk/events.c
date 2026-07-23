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

extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern GtkWidget *main_window;

static void rrgtk_handle_connection_event(const char *event, void *data, irc_conn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   (void)data;

   if (strcmp(event, "http.connected") == 0) {
      update_connection_button(true, conn_button);
   } else if (strcmp(event, "http.disconnected") == 0) {
      update_connection_button(false, conn_button);
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
   (void)cptr;
   (void)user;
   (void)event;
   if (!data) {
      return;
   }

   const char *whois_msg = (const char *)data;
   if (whois_msg && main_window) {
      ui_show_whois_dialog(GTK_WINDOW(main_window), whois_msg);
   }
}

void rrgtk_register_events(void) {
   event_on("http.connected", rrgtk_handle_connection_event, NULL);
   event_on("http.disconnected", rrgtk_handle_connection_event, NULL);
   event_on("http.rig.ptt", rrgtk_handle_ptt_event, NULL);
   event_on("http.rig.freq", rrgtk_handle_freq_event, NULL);
   event_on("http.rig.mode", rrgtk_handle_mode_event, NULL);
   event_on("http.userinfo", rrgtk_handle_userinfo_event, NULL);
   event_on("http.userjoin", rrgtk_handle_userjoin_event, NULL);
   event_on("http.userquit", rrgtk_handle_userquit_event, NULL);
   event_on("http.whois", rrgtk_handle_whois_event, NULL);
}

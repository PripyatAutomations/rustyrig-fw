//
// rrclient/gtk.ptt-btn.c: PTT button stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk.core.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>
#include <rrclient/userlist.h>

extern bool parse_chat_input(GtkButton *button, gpointer entry);         // chat.cmd.c
extern dict *cfg;
extern time_t now;
extern bool ptt_active;
extern const char *login_user;   // connman.c
extern rrconn_t *ws_conn;
extern time_t poll_block_expire, poll_block_delay;
GtkWidget *ptt_button = NULL;

// PTT ack tracking: when the user toggles PTT we show PENDING (yellow) until
// the server's cat.state.ptt echo confirms it, or ui.ptt-ack-timeout expires
// (checked by vfo_update_ui, which reads these two):
bool ptt_button_pending = false;
time_t ptt_button_pending_expire = 0;
extern int cfg_ui_ptt_ack_timeout;          // main.c

// Connection state for the button: grey while offline, colored once online.
// Set via ptt_button_set_online() from events.c
static bool ptt_btn_online = false;
// TOT expired on the server: show orange until the next confirmed PTT update
static bool ptt_btn_tot = false;

#define PTT_LABEL_MAXLEN 16

// Red while active for ANY user: find them in the cached userlist
static struct rr_user *tx_user(void) {
   for (struct rr_user *c = global_userlist; c; c = c->next) {
      if (c->is_ptt) {
         return c;
      }
   }
   return NULL;
}

// True if someone OTHER than us is transmitting
static bool someone_else_transmitting(struct rr_user *talker) {
   return (talker && (!login_user || strcmp(talker->name, login_user) != 0) );
}

// We (the clicking user) may halt a noob's PTT if we're an admin or elmer.
// Both sets of privileges come from our cached userlist data, which the
// server sends as part of the login process.
static bool i_can_halt_noob(const struct rr_user *talker) {
   if (!talker || !strcasestr(talker->privs, "noob") ) {
      return false;
   }

   struct rr_user *me = (login_user ? userlist_find(login_user) : NULL);
   if (!me) {
      return false;
   }

   return (strcasestr(me->privs, "admin") || strcasestr(me->privs, "elmer") );
}

// Apply the current state to the button widget
static void ptt_button_apply(void) {
   if (!ptt_button) {
      return;
   }

   GtkStyleContext *ctx = gtk_widget_get_style_context( GTK_WIDGET(ptt_button) );
   const gchar *label;
   const char *cls;
   struct rr_user *talker = tx_user();

   // grey = offline, yellow = pending, orange = TOT fired, green = idle,
   // red = any user TX, showing their callsign (css in gtk.core.c)
   if (!ptt_btn_online) {
      label = "OFFLINE";
      cls = "ptt-offline";
   } else if (ptt_button_pending) {
      label = "PENDING";
      cls = "ptt-pending";
   } else if (ptt_btn_tot) {
      label = "TOT EXPIRED";
      cls = "ptt-tot";
   } else if (someone_else_transmitting(talker) ) {
      // Show who's on the air, limited to PTT_LABEL_MAXLEN characters.
      // The button has a fixed width so it never resizes.
      static char namebuf[PTT_LABEL_MAXLEN + 1];
      snprintf(namebuf, sizeof(namebuf), "%.*s", PTT_LABEL_MAXLEN, talker->name);
      label = namebuf;
      cls = "ptt-active";
   } else if (ptt_active) {
      label = "PTT ON";
      cls = "ptt-active";
   } else {
      label = "PTT OFF";
      cls = "ptt-idle";
   }

   gtk_button_set_label(GTK_BUTTON(ptt_button), label);
   gtk_style_context_remove_class(ctx, "ptt-active");
   gtk_style_context_remove_class(ctx, "ptt-pending");
   gtk_style_context_remove_class(ctx, "ptt-idle");
   gtk_style_context_remove_class(ctx, "ptt-offline");
   gtk_style_context_remove_class(ctx, "ptt-tot");
   gtk_style_context_add_class(ctx, cls);
}

// Re-evaluate button state (called when the userlist changes, since another
// user starting/stopping TX changes the color)
void ptt_button_refresh(void) {
   ptt_button_apply();
}

// Server TOT expired: orange warning until the next confirmed PTT state arrives
void ptt_button_tot_expired(void) {
   ptt_btn_tot = true;
   ptt_button_pending = false;
   ptt_button_pending_expire = 0;
   ptt_button_apply();
}

// Called from events.c when we connect/authorize or go offline
void ptt_button_set_online(bool online) {
   if (ptt_btn_online == online) {
      return;
   }
   ptt_btn_online = online;

   if (!online) {
      // Going offline resets everything back to grey
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      ptt_btn_tot = false;
      ptt_active = false;
      if (ptt_button) {
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), FALSE);
      }
   }
   ptt_button_apply();
}

void update_ptt_button_ui(GtkToggleButton *button, int active) {
   if (!button) {
      return;
   }

   // Confirmed states from the server clear any pending flag; -1 means
   // "enter pending" from the toggle handler.
   if (active >= 0) {
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      ptt_btn_tot = false;   // confirmed state supersedes a TOT warning
      ptt_active = (active == 1);
   }

   (void)button;   // we always render onto the global ptt_button
   ptt_button_apply();
}

static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   if (!button) {
      return;
   }

   struct rr_user *talker = tx_user();

   // We can't key up while someone else holds PTT. Exception: if they're a
   // noob and we're admin/elmer, we allow it so the server halts their TX
   // (and starts their cooldown).
   if (someone_else_transmitting(talker) && !i_can_halt_noob(talker) ) {
      Log(LOG_AUDIT, "ui.gtk", "PTT ignored: %s is already transmitting", talker->name);
      ui_print(NULL, "{yellow}*** {bright-red}%s{bright-yellow} is already transmitting{reset}", talker->name);

      // Revert the toggle without re-entering this handler
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      g_signal_handlers_block_by_func(button, on_ptt_toggled, NULL);
      gtk_toggle_button_set_active(button, FALSE);
      g_signal_handlers_unblock_by_func(button, on_ptt_toggled, NULL);
      ptt_button_apply();
      return;
   }

   ptt_active = gtk_toggle_button_get_active(button);

   // Enter PENDING until the server echoes cat.state.ptt back to us
   ptt_button_pending = true;
   ptt_button_pending_expire = now + cfg_ui_ptt_ack_timeout;
   update_ptt_button_ui(button, -1);

   poll_block_expire = now + poll_block_delay;

   // Send to server the negated value
   char vfo[2] = { vfo_state_get_active(), '\0' };
   if (!ptt_active) {
      Log(LOG_CRAZY, "ui.gtk", "Turning PTT off");
      ws_send_ptt_cmd(ws_conn, vfo, false);
   } else {
      Log(LOG_CRAZY, "ui.gtk", "Turning PTT on");
      ws_send_ptt_cmd(ws_conn, vfo, true);
   }
}

GtkWidget *ptt_button_create(void) {
   GtkWidget *ptt_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   gtk_widget_set_tooltip_text(ptt_button, "Push To Talk toggle");
   // fixed min width so label changes (callsigns up to 16 chars, PTT OFF/ON,
   // PENDING, TOT EXPIRED) don't resize it
   gtk_widget_set_size_request(ptt_button, 180, -1);

   // try to avoid leaking memory due to buggy GUI code...
   if (!ptt_box || !ptt_button) {
      if (ptt_box) {
         free(ptt_box);
      }

      if (ptt_button) {
         free(ptt_button);
      }
      fprintf(stderr, "problem creating ptt_box or it's widgets. OOM?\n");

      return NULL;
   }
   gtk_box_pack_start(GTK_BOX(ptt_box), ptt_button, FALSE, FALSE, 0);
   g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   // Start out dark grey until we're online with the server
   gtk_style_context_add_class(gtk_widget_get_style_context(ptt_button), "ptt-offline");

   return ptt_box;
}

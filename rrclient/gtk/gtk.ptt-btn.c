#include <rrclient/supervision.h>
#include <rrclient/cmd.h>
//
// rrclient/gtk/gtk.ptt-btn.c: PTT button stuff
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
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.chat.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>
#include <rrclient/userlist.h>
#include <rrclient/rooms.h>
#include <rrclient/audio.h>
#include <rrclient/media.h>

extern bool parse_chat_input(GtkButton *button, gpointer entry);  // chat.cmd.c
extern dict *cfg;
extern time_t now;
extern bool ptt_active;
extern const char *login_user;  // connman.c
extern rrconn_t *ws_conn;
extern time_t poll_block_expire, poll_block_delay;
extern bool dying;              // main.c
extern bool rrclient_ptt_allowed(void);
extern void rrclient_send_ptt_command(rrconn_t *, const char *, bool, const char *);
GtkWidget *ptt_button = NULL;

// PTT ack tracking: when the user toggles PTT we show PENDING (yellow) until
// the server's cat.state.ptt echo confirms it, or ui.ptt-ack-timeout expires
// (checked by the frontend clock via ptt_button_refresh):
bool ptt_button_pending = false;
time_t ptt_button_pending_expire = 0;
// The state we asked the server for; the ack (cat.state.ptt) must match it
// before we leave PENDING, so unrelated/stale echoes don't cancel it
bool ptt_button_pending_state = false;
// Quiet pending: filter contradictory echoes but DON'T show PENDING on the
// button (used after sending PTT off, so a stale "on" ack can't bounce us)
bool ptt_button_pending_quiet = false;
extern int cfg_ui_ptt_ack_timeout;  // main.c
extern int cfg_ui_ptt_hold_delay;   // main.c

static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data);
static gulong ptt_toggled_handler = 0;
static bool ptt_hotkey_held = false;
static bool ptt_hotkey_ack = false;
static bool ptt_hotkey_initial_active = false;
static gint64 ptt_hotkey_pressed_at = 0;

/* A quick tap toggles the PTT lock, matching the TUI. Holding the shortcut keeps PTT active only until release. */
// Connection state for the button: grey while offline, colored once online.
// Set via ptt_button_set_online() from events.c
static bool ptt_btn_online = false;
// TOT expired on the server: show orange TOT EXPIRE until acknowledged
static bool ptt_btn_tot = false;


#define PTT_LABEL_MAXLEN 16

// Red while active for ANY user: find them in the cached userlist
static struct rr_user *tx_user(void) {
   char active_vfo = vfo_state_get_active();

   for (struct rr_user *c = global_userlist ; c ; c = c->next) {
      if (c->room[0] && strcasecmp(c->room, rrclient_current_room()) != 0) {
         continue;
      }

      if (c->is_ptt && c->ptt_vfo == active_vfo && (!c->ptt_room[0] || !strcasecmp(c->ptt_room, rrclient_current_room()))) {
         return c;
      }
   }

   return NULL;
}

// The global widget follows one connection/room/VFO at a time. A retained
// local toggle or pending request belongs only to that displayed context.
// PARITY: rustyrig-www/js/webui.rigctl.js ptt_sync_context().
static rrconn_t *ptt_display_connection;
static char ptt_display_room[128];
static char ptt_display_vfo;
static void ptt_sync_context(void) {
   const char *room = rrclient_current_room();
   char vfo = vfo_state_get_active();

   if (ptt_display_connection == ws_conn && ptt_display_vfo == vfo && !strcasecmp(ptt_display_room, room)) {
      return;
   }
   ptt_display_connection = ws_conn;
   snprintf(ptt_display_room, sizeof(ptt_display_room), "%s", room);
   ptt_display_vfo = vfo;
   ptt_button_pending = false;
   ptt_button_pending_expire = 0;
   ptt_button_pending_quiet = false;
   ptt_hotkey_held = false;
   ptt_hotkey_initial_active = false;
   ptt_hotkey_pressed_at = 0;
   struct rr_user *talker = tx_user();
   ptt_active = talker && login_user && !strcasecmp(talker->name, login_user);

   if (ptt_button) {
      if (ptt_toggled_handler) {
         g_signal_handler_block(ptt_button, ptt_toggled_handler);
      }
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), ptt_active);

      if (ptt_toggled_handler) {
         g_signal_handler_unblock(ptt_button, ptt_toggled_handler);
      }
   }
}

// True if someone OTHER than us is transmitting
static bool someone_else_transmitting(struct rr_user *talker) {
   return(talker && (!login_user || strcasecmp(talker->name, login_user) != 0));
}

/* PARITY: librrprotocol/srv.rigctl.c:ws_ptt_can_override. This is a UI hint; the server checks current account flags. */
static bool i_can_halt_user(const struct rr_user *talker) {
   struct rr_user *me = login_user ? userlist_find(login_user) : NULL;

   if (!talker || !me || strcasestr(talker->privs, "owner")) {
      return false;
   }

   if (strcasestr(me->privs, "owner")) {
      return true;
   }

   if (strcasestr(talker->privs, "admin")) {
      return false;
   }

   if (strcasestr(me->privs, "admin")) {
      return true;
   }

   return !strcasestr(me->privs, "noob") && strcasestr(talker->privs, "noob") && (strcasestr(me->privs, "tx") || strcasestr(me->privs, "elmer"));
}

// Apply the current state to the button widget
static void ptt_button_apply(void) {
   ptt_sync_context();

   if (!ptt_button) {
      return;
   }

   GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(ptt_button));
   const gchar *label;
   const char *cls;
   struct rr_user *talker = tx_user();

   bool own_tx = ptt_active || (talker && !someone_else_transmitting(talker));
   vfo_controls_set_ptt_state(ptt_active || talker, own_tx);

   // grey = offline, yellow = pending, orange = TOT fired, green = idle,
   // red = any user TX, showing their callsign (css in gtk.core.c)
   if (rrclient_room_offline(rrclient_current_room())) {
      label = "OFFLINE";
      cls = "ptt-offline";
   } else if (ptt_btn_tot) {
      label = "TOT EXPIRE";
      cls = "ptt-tot";
   } else if (!ptt_btn_online || rrclient_room_offline(rrclient_current_room())) {
      label = "OFFLINE";
      cls = "ptt-offline";
   } else if (someone_else_transmitting(talker)) {
      // Another user is on the air even if a local request is still pending.
      // The shared rig state is the most useful status to show here.
      static char namebuf[PTT_LABEL_MAXLEN + 1];
      snprintf(namebuf, sizeof(namebuf), "%.*s", PTT_LABEL_MAXLEN, talker->name);
      label = namebuf;
      cls = "ptt-active";
   } else if (!own_tx && ws_conn && ws_conn->is_ws && media_have_priv("noob") && !rrclient_supervision_granted(ws_conn, rrclient_current_room())) {
      label = "REQUEST";
      cls = "ptt-offline";
   } else if (ptt_button_pending && !ptt_button_pending_quiet) {
      label = "PENDING";
      cls = "ptt-pending";
   } else if (own_tx) {
      // We're the talker: show our callsign (red, same as anyone else's TX)
      static char namebuf[PTT_LABEL_MAXLEN + 1];
      snprintf(namebuf, sizeof(namebuf), "%.*s", PTT_LABEL_MAXLEN, talker ? talker->name : login_user);
      label = namebuf;
      cls = "ptt-active";
   } else {
      label = "PTT OFF";
      cls = "ptt-idle";
   }

   gtk_widget_set_sensitive(ptt_button, !rrclient_room_offline(rrclient_current_room()));

   if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(ptt_button)), label)) {
      gtk_button_set_label(GTK_BUTTON(ptt_button), label);
   }

   if (!gtk_style_context_has_class(ctx, cls)) {
      gtk_style_context_remove_class(ctx, "ptt-active");
      gtk_style_context_remove_class(ctx, "ptt-pending");
      gtk_style_context_remove_class(ctx, "ptt-idle");
      gtk_style_context_remove_class(ctx, "ptt-offline");
      gtk_style_context_remove_class(ctx, "ptt-tot");
      gtk_style_context_add_class(ctx, cls);
   }
}

// Re-evaluate button state (called when the userlist changes, since another
// user starting/stopping TX changes the color)
void ptt_button_refresh(void) {
   ptt_sync_context();

   if (ptt_button_pending && ptt_button_pending_expire && now >= ptt_button_pending_expire) {
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      ptt_button_pending_quiet = false;
      struct rr_user *talker = tx_user();
      ptt_button_set_state(talker && !someone_else_transmitting(talker));

      return;
   }
   struct rr_user *talker = tx_user();

   if (ptt_btn_online && !ptt_btn_tot && !ptt_button_pending && talker && !someone_else_transmitting(talker) && !ptt_active) {
      ptt_button_set_state(true);

      return;
   }
   ptt_button_apply();
}

// Server TOT expired: orange warning until clicked to acknowledge
// PARITY: rustyrig-www/js/webui.rigctl.js ptt_tot_expired()
void ptt_button_tot_expired(int tot_secs) {
   ptt_btn_tot = true;
   (void)tot_secs;
   ptt_hotkey_ack = ptt_hotkey_held;
   ptt_hotkey_held = false;
   ptt_button_pending = false;
   ptt_button_pending_expire = 0;
   ptt_button_pending_quiet = false;
   ptt_button_set_state(false);
   ptt_button_apply();
}

// Called from events.c when we connect/authorize or go offline
void ptt_button_set_online(bool online) {
   if (ptt_btn_online == online) {
      return;
   }
   ptt_btn_online = online;

   if (!online) {
      // Going offline clears TX state; a TOT warning still needs acknowledgement.
      ptt_hotkey_held = false;
      ptt_hotkey_initial_active = false;
      ptt_hotkey_pressed_at = 0;
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      ptt_active = false;

      ptt_button_set_state(false);
   }
   ptt_button_apply();
}

// Set the PTT button state programmatically WITHOUT firing the "toggled"
// signal - used when reflecting server echoes (cat.state.ptt) onto the UI.
// Firing the handler here would send a new cat.cmd ptt for every echo and
// the server would broadcast it back: an infinite ping-pong.
// (PARITY: rustyrig-www/js/webui.rigctl.js sets checkbox.checked directly)
void ptt_button_set_state(bool active) {
   ptt_sync_context();

   if (dying) {
      return;
   }

   // Ignore a stale echo while waiting for the state we requested.
   if (ptt_button_pending && active != ptt_button_pending_state) {
      return;
   }
   ptt_button_pending = false;
   ptt_button_pending_expire = 0;
   ptt_button_pending_quiet = false;
   ptt_active = active;

   if (!ptt_button) {
      return;
   }

   GtkToggleButton *btn = GTK_TOGGLE_BUTTON(ptt_button);

   if (gtk_toggle_button_get_active(btn) == active) {
      ptt_button_apply();

      return;
   }

   if (ptt_toggled_handler) {
      g_signal_handler_block(ptt_button, ptt_toggled_handler);
   }
   gtk_toggle_button_set_active(btn, active);

   if (ptt_toggled_handler) {
      g_signal_handler_unblock(ptt_button, ptt_toggled_handler);
   }
   ptt_button_apply();
}

void update_ptt_button_ui(GtkToggleButton *button, int active) {
   if (!button || dying) {
      return;
   }

   // Confirmed states from the server clear any pending flag; -1 means
   // "enter pending" from the toggle handler.
   if (active >= 0) {
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
      ptt_button_pending_quiet = false;
      ptt_active = (active == 1);
   }

   (void)button;                // we always render onto the global ptt_button
   ptt_button_apply();
}

/* PTT is a rig operation, never a status-tab/chat-room operation.
 * The media control room follows the selected rig even when a different
 * notebook page is active. Reject non-TX rooms before sending key-down. */
static const char *ptt_target_room(void) {
   const char *room = rrclient_media_active_room();
   if (room && *room && rrclient_room_tx_control(room)) return room;
   room = gtk_chat_current_room();
   if (room && *room && rrclient_room_tx_control(room)) return room;
   return NULL;
}
static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   if (!button) {
      return;
   }

   if (ptt_btn_tot) {
      ptt_btn_tot = false;
      ptt_button_set_state(ptt_active);

      return;
   }

   if (gtk_toggle_button_get_active(button) && !rrclient_ptt_allowed()) {
      Log(LOG_INFO, "ui.gtk", "PTT ON inhibited: TX power slider is 0%%");
      ui_print(NULL, "*** PTT disabled while TX power is 0%%");
      ptt_button_set_state(false);
      return;
   }
   const char *target_room = ptt_target_room();
   if (!target_room && gtk_toggle_button_get_active(button)) {
      Log(LOG_WARN, "ui.gtk", "PTT rejected locally: no TX-control room for selected rig (tab=%s, media=%s)",
         gtk_chat_current_room() ? gtk_chat_current_room() : "(status)",
         rrclient_media_active_room() ? rrclient_media_active_room() : "(none)");
      ui_print(NULL, "*** PTT requires an active rig TX-control room; select the rig before transmitting");
      ptt_button_set_state(false);
      return;
   }
   if (!target_room) target_room = rrclient_media_active_room();
   if (!target_room || !*target_room) target_room = rrclient_current_room();
   struct rr_user *talker = tx_user();

   if (!ptt_active && !someone_else_transmitting(talker) && ws_conn && ws_conn->is_ws && media_have_priv("noob") &&
      !rrclient_supervision_granted(ws_conn, rrclient_current_room())) {
      char vfo[2] = {
         vfo_state_get_active(), 0
      };
      rrclient_send_ptt_command(ws_conn, vfo, true, target_room);
      ptt_button_set_state(false);

      return;
   }

   // Clicking a held transmitter requests STOP only, never a takeover.
   if (someone_else_transmitting(talker) && !i_can_halt_user(talker)) {
      Log(LOG_AUDIT, "ui.gtk", "PTT ignored: %s is already transmitting", talker->name);
      ui_print(NULL, "\00308*** \00304%s\00308 is already transmitting\017", talker->name);

      // Revert the toggle without re-entering this handler
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;

      if (ptt_toggled_handler) {
         g_signal_handler_block(button, ptt_toggled_handler);
      }
      gtk_toggle_button_set_active(button, FALSE);

      if (ptt_toggled_handler) {
         g_signal_handler_unblock(button, ptt_toggled_handler);
      }
      ptt_button_apply();

      return;
   }

   ptt_active = someone_else_transmitting(talker) ? false : gtk_toggle_button_get_active(button);

   // Clicked while a request is still PENDING: cancel it. Clear the pending
   // state; the rest of this handler sends the (now opposite) command.
   if (ptt_button_pending) {
      Log(LOG_CRAZY, "ui.gtk", "PTT clicked while pending: cancelling request");
      ptt_button_pending = false;
      ptt_button_pending_expire = 0;
   }

   poll_block_expire = now + poll_block_delay;

   // Send the new state to the server
   char vfo[2] = {
      vfo_state_get_active(), '\0'
   };

   if (!ptt_active) {
      // Turning OFF needs no visible PENDING: off is the safe default, apply
      // it immediately. We DO keep a quiet pending filter until the server's
      // matching "off" ack, so a stale "on" ack can't bounce the button.
      Log(LOG_CRAZY, "ui.gtk", "Turning PTT off");
      update_ptt_button_ui(button, 0);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), false);
      ptt_button_pending = true;
      ptt_button_pending_state = false;
      ptt_button_pending_quiet = true;
      ptt_button_pending_expire = now + cfg_ui_ptt_ack_timeout;
      rrclient_send_ptt_command(ws_conn, vfo, false, target_room);
   } else {
      // Start the local TX encoder before asking the server to key the rig.
      // The server starts its decoder before sending the matching cat.state
      // acknowledgement, so keeping this gate ahead of the command ensures
      // the first spoken samples have a live path when the button turns red.
      const char *tx_codec = rrclient_media_current_codec(true);

      if (!tx_codec || audio_switch_codec(tx_codec, true)) {
         Log(LOG_WARN, "ui.gtk", "PTT refused: TX audio encoder is not ready");
         ui_print(NULL, "\00308*** TX audio is not ready; select a TX codec first\017");
         ptt_active = false;

         if (ptt_toggled_handler) {
            g_signal_handler_block(button, ptt_toggled_handler);
         }
         gtk_toggle_button_set_active(button, FALSE);

         if (ptt_toggled_handler) {
            g_signal_handler_unblock(button, ptt_toggled_handler);
         }
         ptt_button_apply();

         return;
      }
      Log(LOG_CRAZY, "ui.gtk", "Turning PTT on");
      // Enter PENDING until the server echoes cat.state.ptt back to us
      ptt_button_pending = true;
      ptt_button_pending_state = ptt_active;
      ptt_button_pending_quiet = false;
      ptt_button_pending_expire = now + cfg_ui_ptt_ack_timeout;
      update_ptt_button_ui(button, -1);
      rrclient_send_ptt_command(ws_conn, vfo, true, target_room);
   }
}

bool ptt_button_hotkey_toggle(void) {
   if (!ptt_button) {
      return false;
   }
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ptt_button)));

   return true;
}

bool ptt_button_hotkey_press(void) {
   if (rrclient_room_offline(rrclient_current_room())) {
      return false;
   }

   if (!ptt_button) {
      return false;
   }

   if (ptt_hotkey_held || ptt_hotkey_ack) {
      return true;
   }

   if (ptt_btn_tot) {
      ptt_btn_tot = false;
      ptt_hotkey_ack = true;
      ptt_button_set_state(ptt_active);

      return true;
   }
   ptt_hotkey_held = true;
   ptt_hotkey_initial_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ptt_button));
   ptt_hotkey_pressed_at = g_get_monotonic_time();

   if (!ptt_hotkey_initial_active) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), TRUE);
   }

   return true;
}

bool ptt_button_hotkey_release(void) {
   if (ptt_hotkey_ack) {
      ptt_hotkey_ack = false;

      return true;
   }

   if (!ptt_button || !ptt_hotkey_held) {
      return false;
   }
   gint64 elapsed = g_get_monotonic_time() - ptt_hotkey_pressed_at;
   gint64 hold_delay = (gint64) cfg_ui_ptt_hold_delay * 1000;
   bool quick_tap = elapsed >= 0 && elapsed < hold_delay;
   ptt_hotkey_held = false;
   ptt_hotkey_pressed_at = 0;

   /* A short press is a lock toggle. A held shortcut always keys down only for the duration of the key hold. */
   bool desired_active = quick_tap ? !ptt_hotkey_initial_active : false;

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ptt_button)) != desired_active) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), desired_active);
   }

   return true;
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
   // The main window can be rebuilt during a reconnect or shutdown.  Let
   // GTK clear this borrowed pointer when the widget is finalized so event
   // callbacks cannot update a stale button.
   g_object_add_weak_pointer(G_OBJECT(ptt_button), (gpointer *) &ptt_button);
   ptt_toggled_handler = g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   // Start out dark grey until we're online with the server
   gtk_style_context_add_class(gtk_widget_get_style_context(ptt_button), "ptt-offline");
   /* The connection may have completed before the GTK widget was created. Apply the cached connection state immediately instead of waiting for a later click or
    * server echo to repaint the button. */
   ptt_button_apply();

   return ptt_box;
}

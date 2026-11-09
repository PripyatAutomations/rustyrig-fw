#include "gtk.chat-context.h"
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
//
// rrclient/gtk/gtk.core.c: Core of GTK gui
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <librrprotocol/crypto.h>
#include <rrclient/identity.store.h>
#include <rrclient/tls.pin.h>
#include <rrclient/password.change.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/media.h>
#include <rrclient/vfo.h>
#include <rrclient/vfo.snapshot.h>
#include <rrclient/local.bridge.h>
#include <rrclient/gtk/gtk.floating-panel.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.kiwi-panel.h"
#include "gtk.clock.h"
#include "gtk.identity.h"
#include "gtk.window.h"
#include "gtk.screensaver.h"
#include <rrclient/gtk/gtk.connecting.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include <rrclient/gtk/gtk.connect-button.h>
#include <rrclient/gtk/gtk.serverpick.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include <rrclient/gtk/gtk.audio-routing.h>
#include <rrclient/gtk/gtk.receiver-directory.h>
#include <rrclient/gtk/gtk.resources.h>
#include <rrclient/gtk/gtk.files.h>
#include <rrclient/gtk/gtk.file-editor.h>
#include <rrclient/gtk/gtk.tabs.h>
#include <rrclient/gtk/gtk.help.h>


extern dict *cfg;
extern time_t now;
extern bool dying;              // main.c
GtkWidget *main_window = NULL;
GtkWidget *conn_button = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *main_notebook = NULL;
GtkWidget *rr_main_toolbar = NULL;
GtkWidget *status_tab = NULL;
GtkWidget *log_tab = NULL;
GtkWidget *host_log_tab = NULL;
GtkCssProvider *css_provider = NULL;
bool cfg_use_gtk = true;        // Default to using GTK3
extern GtkWidget *init_log_tab(void);
extern GtkWidget *init_host_log_tab(void);
extern void rr_module_frontend_stop(void);
extern GtkWidget *init_admin_tab(void);
extern bool chat_init(void);    // gtk.chat.c
bool cfg_fullscreen = false;

// ------------------------------------------------------------------
// Frontend module glue: the ops table the core client sees, plus the
// module main-loop ownership (gtk_main).
//
// The module interacts with core client state ONLY through the event bus
// (event_on*/event_emit) and the frontend host API in rrclient/frontend.h.
// ------------------------------------------------------------------

#include <rrclient/frontend.h>
#include <rrclient/connman.h>
#include <dlfcn.h>

extern bool ptt_button_hotkey_toggle(void);  // gtk.ptt-btn.c
extern bool syslog_clear(void); // gtk.syslog.c
extern GtkTextBuffer *text_buffer;  // gtk.chat.c
extern GtkWidget *rx_vol_slider; // gtk.vol-box.c
extern GtkWidget *chat_textview; // gtk.chat.c
extern GtkWidget *admin_tab, *config_tab; // gtk.core.c (this file)
extern bool cfg_gtkcss_init(void);  // cfg.gtkcss.c

static void frontend_gtk_apply_vfo_state(const char *vfo, const char *room, bool available, long freq, const char *mode, int width, int power, bool ptt) {
   GtkWidget *entry = freq_entry;

   if (entry) {
      GtkFreqEntry *fe = GTK_FREQ_ENTRY(entry);
      gtk_widget_set_tooltip_text(entry, available ? "Observed frequency" : "Frequency unavailable");

      if (!available) {
         gtk_freq_entry_set_unavailable(fe);
         modebox_update_state(mode, width);

         return;
      }
      char selection[256];
      snprintf(selection, sizeof(selection), "%s/%s", room, vfo);
      const char *displayed = g_object_get_data(G_OBJECT(entry), "rr-displayed-vfo");

      bool switching = !displayed || strcmp(displayed, selection);
      /* Until a new tuning setpoint is observed, do not repaint the entry from an older bridge/poll echo. That stale repaint made the next digit +/- operation
       * jump back to the receiver's original frequency. Scope the guard to this displayed VFO; never carry it across rooms. */
      extern time_t freqentry_last_send;

      if (!switching && freqentry_last_send > 0 && gtk_freq_entry_get_frequency(fe) == (unsigned long)freq) {
         /* The bridge acknowledged the latest displayed setpoint. */
         freqentry_last_send = 0;
      }
      bool recent_local_tune = !switching && freqentry_last_send > 0 && now >= freqentry_last_send && now - freqentry_last_send < 5;
      bool awaiting_echo = recent_local_tune && gtk_freq_entry_get_frequency(fe) != (unsigned long)freq;

      if (switching || (!gtk_freq_entry_is_editing(fe) && !awaiting_echo)) {
         g_object_set_data_full(G_OBJECT(entry), "rr-displayed-vfo", g_strdup(selection), g_free);
         gtk_freq_entry_set_frequency(fe, freq);
      }
   }
   modebox_update_state(mode, width);
}

static struct {
   guint source;
   char vfo[4], mode[32], room[128], server[1024];
   long freq;
   int width, power;
   bool ptt, has_mode, available;
} pending_vfo_paint;
static gboolean frontend_gtk_flush_vfo(gpointer unused) {
   pending_vfo_paint.source = 0;

   /* The latest core snapshot is authoritative; no live core lookup in the GTK paint callback. */
   if (!dying) {
      frontend_gtk_apply_vfo_state(pending_vfo_paint.vfo, pending_vfo_paint.room, pending_vfo_paint.available, pending_vfo_paint.freq, pending_vfo_paint.
         has_mode ? pending_vfo_paint.mode : NULL, pending_vfo_paint.width, pending_vfo_paint.power, pending_vfo_paint.ptt);
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_vfo_state(const char *vfo, long freq, const char *mode, int width, int power, bool ptt) {
   if (dying) {
      return;
   }
   const char *server = rrclient_selected_server();
   const char *room = rrclient_media_active_room();
   snprintf(pending_vfo_paint.vfo, sizeof(pending_vfo_paint.vfo), "%s", vfo ? vfo : "");
   snprintf(pending_vfo_paint.mode, sizeof(pending_vfo_paint.mode), "%s", mode ? mode : "");
   snprintf(pending_vfo_paint.server, sizeof(pending_vfo_paint.server), "%s", server ? server : "");
   snprintf(pending_vfo_paint.room, sizeof(pending_vfo_paint.room), "%s", room ? room : "");
   pending_vfo_paint.has_mode = mode != NULL;
   pending_vfo_paint.available = freq > 0;
   pending_vfo_paint.freq = freq;
   pending_vfo_paint.width = width;
   pending_vfo_paint.power = power;
   pending_vfo_paint.ptt = ptt;

   if (!pending_vfo_paint.source) {
      pending_vfo_paint.source = g_idle_add(frontend_gtk_flush_vfo, NULL);
   }
}

/* All of these frontend operations may originate in the core callback path. Route them through frontend_gtk_ui_post() below, never synchronously paint. */
static void frontend_gtk_freq_set(long freq);
static void frontend_gtk_mode_set(const char *mode);
static void frontend_gtk_conn_button(int connected);
static void frontend_gtk_ptt_set_online(bool online);
static void frontend_gtk_ptt_set_state(bool active);
static void frontend_gtk_ptt_tot_expired(int tot_secs);
static void frontend_gtk_ptt_refresh(void);

static void frontend_gtk_codec_set_active(bool is_tx, const char *codec);
static void frontend_gtk_syslog_clear(void);
static void frontend_gtk_focus_tab(const char *tab);
static void frontend_gtk_switch_window(int id);
static void frontend_gtk_alert(const char *message);
static void frontend_gtk_edit_config(const char *path);
static void frontend_gtk_bell(void);
static void frontend_gtk_notify(const char *title, const char *message);
static void frontend_gtk_show_server_chooser(void);
static void frontend_gtk_webcam_show(bool show);

static gboolean frontend_gtk_ptt_hotkey_on_owner(gpointer unused) {
   (void)unused;

   if (!dying) {
      ptt_button_hotkey_toggle();
   }

   return G_SOURCE_REMOVE;
}

static GMainContext *frontend_gtk_event_context;
static GThread *frontend_gtk_owner;
static bool frontend_gtk_domain_registered;
static bool frontend_gtk_stopped;

static void frontend_gtk_assert_owner(void) {
#ifndef G_DISABLE_ASSERT
   g_assert(g_thread_self() == frontend_gtk_owner);
#else

   if (G_UNLIKELY(g_thread_self() != frontend_gtk_owner)) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_CRITICAL, "GTK operation executed on wrong thread: actual=%p expected=%p", (void *)g_thread_self(), (void *)
         frontend_gtk_owner);
   }
#endif
}

static bool frontend_gtk_ptt_hotkey_toggle(void) {
   if (g_thread_self() == frontend_gtk_owner) {
      return ptt_button_hotkey_toggle();
   }

   /* Return "handled" rather than letting core issue a second TX command. */
   if (frontend_gtk_event_context) {
      GSource *source = g_idle_source_new();
      g_source_set_callback(source, frontend_gtk_ptt_hotkey_on_owner, NULL, NULL);
      g_source_attach(source, frontend_gtk_event_context);
      g_source_unref(source);
   }

   return true;
}

static void frontend_gtk_codec_set_active_owner(bool is_tx, const char *codec) {
   codec_picker_set_active(is_tx, codec);
}

static void frontend_gtk_syslog_clear_owner(void) {
   syslog_clear();
}

static void frontend_gtk_focus_tab_owner(const char *tab) {
   GtkWidget *page = NULL;

   if (!strcasecmp(tab, "explorer") || !strcasecmp(tab, "resource explorer")) {
      event_emit("client.resources.open", NULL, NULL);

      return;
   } else if (!strcasecmp(tab, "admin")) {
      page = admin_tab;
   } else if (!strcasecmp(tab, "config")) {
      page = config_tab;
   } else if (!strcasecmp(tab, "log")) {
      page = log_tab;
   } else if (!strcasecmp(tab, "host log")) {
      page = host_log_tab;
   } else if (!strcasecmp(tab, "status")) {
      page = status_tab;
   }

   if (!page || !main_notebook) {
      return;
   }
   int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), page);

   if (index != -1) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
   }
}

static void frontend_gtk_switch_window_owner(int id) {
   gtk_tabs_select(GTK_NOTEBOOK(main_notebook), id);
}

static bool frontend_gtk_confirm_dialog(const char *message) {
   /* A synchronous confirmation cannot be marshaled without blocking core. Deny rather than create a modal GTK window on the wrong thread. */
   if (g_thread_self() != frontend_gtk_owner) {
      Log(LOG_WARN, "gtk.thread", "Synchronous confirmation requested outside GTK thread; action denied");

      return false;
   }
   GtkWindow *parent = NULL;

   if (main_window && GTK_IS_WINDOW(main_window)) {
      parent = GTK_WINDOW(main_window);
   }

   return ui_confirm_dialog(parent, message);
}

static void frontend_gtk_alert_owner(const char *message) {
   alert_dialog(GTK_WINDOW(main_window), MSG_ERROR, message ? message : "");
}

static void frontend_gtk_quit_request(const char *event, const char *data, rrconn_t *cptr, void *user) {
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;

   if (!ui_confirm_dialog(parent, "Confirm quit?")) {
      return;
   }

   if (rrclient_config_is_dirty()) {
      GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
         "Configuration has unsaved changes. Save before quitting?");
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT);
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_ACCEPT);
      gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
      gint response = gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);

      if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) {
         return;
      }

      if (response == GTK_RESPONSE_ACCEPT) {
         gboolean saved = rrclient_config_save();

         if (!saved) {
            GtkWidget *error = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
               "Unable to save the client configuration. Quit cancelled.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);

            return;
         }
         rrclient_config_clear_dirty();
      }
   }
   extern bool dying;
   dying = true;
}

static void frontend_gtk_edit_config_owner(const char *path) {
   gui_file_editor_open_config(path);
}

static void frontend_gtk_bell_owner(void) {
   GdkDisplay *display = gtk_widget_get_display(main_window ? main_window : chat_textview);

   if (display) {
      gdk_display_beep(display);
   }
}

static void frontend_gtk_notify_owner(const char *title, const char *message) {
#ifdef USE_LIBNOTIFY
   ui_message_notify(title, message);
#endif
}

static void frontend_gtk_show_server_chooser_owner(void) {
   gtk_show_server_chooser();
}

extern void gtk_webcam_show(bool show);   // gtk.webcam.c
static void frontend_gtk_webcam_show_owner(bool show) {
   gtk_webcam_show(show);
}

static guint frontend_gtk_roster_source;
static gboolean frontend_gtk_roster_queued;
static gboolean frontend_gtk_flush_roster(gpointer unused) {
   (void)unused;
   frontend_gtk_assert_owner();
   frontend_gtk_roster_queued = false;
   frontend_gtk_roster_source = 0;

   if (!dying) {
      userlist_redraw_gtk();
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_userlist_redraw(void) {
   // One network sweep can deliver many observations. Core callers publish a
   // single event; GTK-thread listeners coalesce and schedule the widget paint.
   if (!dying) {
      event_emit_message(&(rr_event_message_t) {
         .name = "client.ui.roster.redraw", .sender = "core", .payload = NULL, .length = 0
      });
   }
}
static void frontend_gtk_roster_redraw_event(const rr_event_message_t *message, void *unused) {
   (void)message;
   (void)unused;
   frontend_gtk_assert_owner();

   if (dying || frontend_gtk_roster_queued) {
      return;
   }
   frontend_gtk_roster_queued = true;
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_flush_roster, NULL, NULL);
   frontend_gtk_roster_source = g_source_attach(source, frontend_gtk_event_context);
   g_source_unref(source);
}

/* Legacy frontend callbacks can originate on the core transport thread. Queue owned arguments before touching GTK widgets. */

typedef enum {
   GTK_UI_VISIBLE, GTK_UI_ROOM_STATE, GTK_UI_ROOM_VFOS,
   GTK_UI_CHAT_CLEAR, GTK_UI_RX_VOLUME, GTK_UI_FREQ,
   GTK_UI_MODE, GTK_UI_CONNECTION, GTK_UI_PTT_ONLINE,
   GTK_UI_PTT_STATE, GTK_UI_PTT_TOT, GTK_UI_PTT_REFRESH
} GtkUiAction;
typedef struct {
   GtkUiAction action;
   char *room;
   int value;
   long freq;
} GtkUiDelivery;
static gboolean frontend_gtk_ui_deliver(gpointer opaque) {
   GtkUiDelivery *d = opaque;

   if (dying) {
      return G_SOURCE_REMOVE;
   }

   switch (d->action) {
      case GTK_UI_VISIBLE: {
         userlist_set_visible(d->value != 0);
         break;
      }
      case GTK_UI_ROOM_STATE: {
         userlist_room_state_changed(d->room);
         break;
      }
      case GTK_UI_ROOM_VFOS: {
         gtk_chat_room_vfos_changed(d->room);
         break;
      }
      case GTK_UI_CHAT_CLEAR: {
         if (text_buffer) {
            gtk_text_buffer_set_text(text_buffer, "", -1);
         }
         break;
      }
      case GTK_UI_RX_VOLUME: {
         if (rx_vol_slider && GTK_IS_RANGE(rx_vol_slider)) {
            gtk_range_set_value(GTK_RANGE(rx_vol_slider), d->value);
         }
         break;
      }
      case GTK_UI_FREQ: {
         if (freq_entry && !gtk_freq_entry_is_editing(GTK_FREQ_ENTRY(freq_entry))) {
            gtk_freq_entry_set_frequency(GTK_FREQ_ENTRY(freq_entry), d->freq);
         }
         break;
      }
      case GTK_UI_MODE: {
         modebox_update_state(d->room, 0);
         break;
      }
      case GTK_UI_CONNECTION: {
         rr_connect_button_update_all(d->value);
         break;
      }
      case GTK_UI_PTT_ONLINE: {
         ptt_button_set_online(d->value != 0);
         break;
      }
      case GTK_UI_PTT_STATE: {
         ptt_button_set_state(d->value != 0);
         break;
      }
      case GTK_UI_PTT_TOT: {
         ptt_button_tot_expired(d->value);
         break;
      }
      case GTK_UI_PTT_REFRESH: {
         ptt_button_refresh();
         break;
      }
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_ui_delivery_free(gpointer opaque) {
   GtkUiDelivery *d = opaque;
   g_free(d->room);
   g_free(d);
}
static void frontend_gtk_ui_post(GtkUiAction action, const char *room, int value) {
   GtkUiDelivery *d = g_new0(GtkUiDelivery, 1);
   d->action = action;
   d->room = g_strdup(room);
   d->value = value;

   if (g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_ui_deliver(d);
      frontend_gtk_ui_delivery_free(d);

      return;
   }

   if (!frontend_gtk_event_context) {
      frontend_gtk_ui_delivery_free(d);

      return;
   }
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_ui_deliver, d, frontend_gtk_ui_delivery_free);
   g_source_attach(source, frontend_gtk_event_context);
   g_source_unref(source);
}
static void frontend_gtk_userlist_set_visible(bool visible) {
   frontend_gtk_ui_post(GTK_UI_VISIBLE, NULL, visible);
}
static void frontend_gtk_room_state_changed(const char *room) {
   frontend_gtk_ui_post(GTK_UI_ROOM_STATE, room, 0);
}
static void frontend_gtk_userlist_room_vfos_changed(const char *room) {
   frontend_gtk_ui_post(GTK_UI_ROOM_VFOS, room, 0);
}
static void frontend_gtk_chat_clear(void) {
   frontend_gtk_ui_post(GTK_UI_CHAT_CLEAR, NULL, 0);
}
static void frontend_gtk_rx_volume(int value) {
   frontend_gtk_ui_post(GTK_UI_RX_VOLUME, NULL, value);
}
static void frontend_gtk_freq_set(long freq) {
   GtkUiDelivery *d = g_new0(GtkUiDelivery, 1);
   d->action = GTK_UI_FREQ;
   d->freq = freq;

   /* The common queue helper accepts an int; preserve long frequencies. */
   if (g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_ui_deliver(d);
      frontend_gtk_ui_delivery_free(d);
   } else if (frontend_gtk_event_context) {
      GSource *source = g_idle_source_new();
      g_source_set_callback(source, frontend_gtk_ui_deliver, d, frontend_gtk_ui_delivery_free);
      g_source_attach(source, frontend_gtk_event_context);
      g_source_unref(source);
   } else {
      frontend_gtk_ui_delivery_free(d);
   }
}
static void frontend_gtk_mode_set(const char *mode) {
   frontend_gtk_ui_post(GTK_UI_MODE, mode, 0);
}
static void frontend_gtk_conn_button(int connected) {
   frontend_gtk_ui_post(GTK_UI_CONNECTION, NULL, connected);
}
static void frontend_gtk_ptt_set_online(bool online) {
   frontend_gtk_ui_post(GTK_UI_PTT_ONLINE, NULL, online);
}
static void frontend_gtk_ptt_set_state(bool active) {
   frontend_gtk_ui_post(GTK_UI_PTT_STATE, NULL, active);
}
static void frontend_gtk_ptt_tot_expired(int secs) {
   frontend_gtk_ui_post(GTK_UI_PTT_TOT, NULL, secs);
}
static void frontend_gtk_ptt_refresh(void) {
   frontend_gtk_ui_post(GTK_UI_PTT_REFRESH, NULL, 0);
}

/* One-way frontend operations: own strings and execute on GTK's context. The bool-returning frontend operations deliberately remain separate:
 * synchronous results require an explicit asynchronous protocol. */
typedef enum {
   GTK_OP_CODEC, GTK_OP_CLEAR_LOG, GTK_OP_FOCUS, GTK_OP_WINDOW,
   GTK_OP_ALERT, GTK_OP_EDIT, GTK_OP_BELL, GTK_OP_NOTIFY,
   GTK_OP_CHOOSER, GTK_OP_WEBCAM
} GtkOwnerAction;
typedef struct {
   uint32_t action, a_len, b_len;
   int32_t value;
} GtkOwnerMessage;
static void frontend_gtk_owner_action_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (dying || !message->payload || message->length < sizeof(GtkOwnerMessage)) {
      return;
   }
   GtkOwnerMessage m;
   memcpy(&m, message->payload, sizeof(m));

   if (m.action > GTK_OP_WEBCAM || !m.a_len || !m.b_len) {
      return;
   }
   size_t bytes = message->length - sizeof(m);

   if ((size_t)m.a_len > bytes || (size_t)m.b_len != bytes - m.a_len) {
      return;
   }
   const char *a = (const char *)message->payload + sizeof(m);
   const char *b = a + m.a_len;

   if (a[m.a_len - 1] || b[m.b_len - 1]) {
      return;
   }

   switch ((GtkOwnerAction) m.action) {
      case GTK_OP_CODEC: {
         frontend_gtk_codec_set_active_owner(m.value != 0, a);
         break;
      }
      case GTK_OP_CLEAR_LOG: {
         frontend_gtk_syslog_clear_owner();
         break;
      }
      case GTK_OP_FOCUS: {
         frontend_gtk_focus_tab_owner(a);
         break;
      }
      case GTK_OP_WINDOW: {
         frontend_gtk_switch_window_owner(m.value);
         break;
      }
      case GTK_OP_ALERT: {
         frontend_gtk_alert_owner(a);
         break;
      }
      case GTK_OP_EDIT: {
         frontend_gtk_edit_config_owner(*a ? a : NULL);
         break;
      }
      case GTK_OP_BELL: {
         frontend_gtk_bell_owner();
         break;
      }
      case GTK_OP_NOTIFY: {
         frontend_gtk_notify_owner(a, b);
         break;
      }
      case GTK_OP_CHOOSER: {
         frontend_gtk_show_server_chooser_owner();
         break;
      }
      case GTK_OP_WEBCAM: {
         frontend_gtk_webcam_show_owner(m.value != 0);
         break;
      }
   }
}
static void frontend_gtk_owner_post(GtkOwnerAction action, const char *a, const char *b, int value) {
   size_t al = strlen(a ? a : "") + 1, bl = strlen(b ? b : "") + 1;

   if (al > 65536 || bl > 65536) {
      return;
   }
   GtkOwnerMessage header = {
      (uint32_t) action, (uint32_t) al, (uint32_t) bl, value
   };
   size_t length = sizeof(header) + al + bl;
   char *payload = g_malloc(length);
   memcpy(payload, &header, sizeof(header));
   char *p = payload + sizeof(header);
   memcpy(p, a ? a : "", al);
   memcpy(p + al, b ? b : "", bl);
   rr_event_message_t message = {
      .name = "client.ui.owner.action", .payload = payload, .length = length
   };
   event_emit_message(&message);
   g_free(payload);
}
static void frontend_gtk_codec_set_active(bool is_tx, const char *codec) {
   frontend_gtk_owner_post(GTK_OP_CODEC, codec, NULL, is_tx);
}
static void frontend_gtk_syslog_clear(void) {
   frontend_gtk_owner_post(GTK_OP_CLEAR_LOG, NULL, NULL, 0);
}
static void frontend_gtk_focus_tab(const char *tab) {
   frontend_gtk_owner_post(GTK_OP_FOCUS, tab, NULL, 0);
}
static void frontend_gtk_switch_window(int id) {
   frontend_gtk_owner_post(GTK_OP_WINDOW, NULL, NULL, id);
}
static void frontend_gtk_alert(const char *message) {
   frontend_gtk_owner_post(GTK_OP_ALERT, message, NULL, 0);
}
static void frontend_gtk_edit_config(const char *path) {
   frontend_gtk_owner_post(GTK_OP_EDIT, path, NULL, 0);
}
static void frontend_gtk_bell(void) {
   frontend_gtk_owner_post(GTK_OP_BELL, NULL, NULL, 0);
}
static void frontend_gtk_notify(const char *title, const char *message) {
   frontend_gtk_owner_post(GTK_OP_NOTIFY, title, message, 0);
}
static void frontend_gtk_show_server_chooser(void) {
   frontend_gtk_owner_post(GTK_OP_CHOOSER, NULL, NULL, 0);
}
static void frontend_gtk_webcam_show(bool show) {
   frontend_gtk_owner_post(GTK_OP_WEBCAM, NULL, NULL, show);
}

/* Chat widget mutations are always delivered to GTK's owning thread. Never pass a borrowed core dictionary/string across the boundary. */
typedef enum {
   GTK_CHAT_ADD, GTK_CHAT_REMOVE, GTK_CHAT_TOPIC, GTK_CHAT_STATUS,
   GTK_CHAT_AUTH, GTK_CHAT_QUERY, GTK_CHAT_VFOS
} GtkChatAction;
typedef struct {
   GtkChatAction action;
   GtkChatContext context;
   char *a, *b;
} GtkChatCommand;
static gboolean frontend_gtk_chat_command_run(gpointer opaque) {
   GtkChatCommand *cmd = opaque;
   const GtkChatContext *previous = gtk_chat_context_push(&cmd->context);

   switch (cmd->action) {
      case GTK_CHAT_ADD: {
         gtk_chat_room_add(cmd->a);
         break;
      }
      case GTK_CHAT_REMOVE: {
         gtk_chat_room_remove(cmd->a);
         break;
      }
      case GTK_CHAT_TOPIC: {
         gtk_chat_room_set_topic(cmd->a, cmd->b);
         break;
      }
      case GTK_CHAT_STATUS: {
         gtk_chat_show_status();
         break;
      }
      case GTK_CHAT_AUTH: {
         gtk_chat_set_authoritative_room(cmd->a);
         break;
      }
      case GTK_CHAT_QUERY: {
         gtk_chat_query_add(cmd->a);
         break;
      }
      case GTK_CHAT_VFOS: {
         gtk_chat_room_vfos_changed(cmd->a);
         break;
      }
   }

   gtk_chat_context_pop(previous);

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_chat_command_free(gpointer opaque) {
   GtkChatCommand *cmd = opaque;
   gtk_chat_context_clear(&cmd->context);
   g_free(cmd->a);
   g_free(cmd->b);
   g_free(cmd);
}
static void frontend_gtk_chat_command(GtkChatAction action, const char *a, const char *b) {
   GtkChatCommand *cmd = g_new0(GtkChatCommand, 1);
   cmd->action = action;
   gtk_chat_context_capture(&cmd->context, a);
   cmd->a = g_strdup(a);
   cmd->b = g_strdup(b);

   if (!frontend_gtk_event_context || g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_chat_command_run(cmd);
      frontend_gtk_chat_command_free(cmd);

      return;
   }
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_chat_command_run, cmd, frontend_gtk_chat_command_free);
   g_source_attach(source, frontend_gtk_event_context);
   g_source_unref(source);
}
static void frontend_gtk_chat_room_add(const char *room) {
   frontend_gtk_chat_command(GTK_CHAT_ADD, room, NULL);
}
static void frontend_gtk_chat_room_remove(const char *room) {
   frontend_gtk_chat_command(GTK_CHAT_REMOVE, room, NULL);
}
static void frontend_gtk_chat_room_topic(const char *room, const char *topic) {
   frontend_gtk_chat_command(GTK_CHAT_TOPIC, room, topic);
}
static void frontend_gtk_chat_show_status(void) {
   frontend_gtk_chat_command(GTK_CHAT_STATUS, NULL, NULL);
}
static void frontend_gtk_chat_set_authoritative_room(const char *room) {
   frontend_gtk_chat_command(GTK_CHAT_AUTH, room, NULL);
}
static void frontend_gtk_chat_query_add(const char *who) {
   frontend_gtk_chat_command(GTK_CHAT_QUERY, who, NULL);
}
static void frontend_gtk_chat_room_vfos_changed(const char *room) {
   frontend_gtk_chat_command(GTK_CHAT_VFOS, room, NULL);
}
static const char *frontend_gtk_chat_current_room(void) {
   /* Never return a borrowed GtkWidget-backed string to core. Core callers must use their authoritative selected-room state instead. */
   if (g_thread_self() != frontend_gtk_owner) {
      return NULL;
   }

   return gtk_chat_current_room();
}

static void ui_print_gtk_literal(const char *window, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   ui_print_gtk(window, fmt, args);
   va_end(args);
}

/* A core/network callback must never edit a GTK text buffer directly. Snapshot printf output before handing it to the GUI owner thread. */
typedef struct {
   char *window;
   char *message;
   GtkChatContext context;
} GtkQueuedPrint;

static gboolean frontend_gtk_queued_print(gpointer opaque) {
   GtkQueuedPrint *print = opaque;

   if (!dying) {
      const GtkChatContext *previous = gtk_chat_context_push(&print->context);
      ui_print_gtk_literal(print->window, "%s", print->message);
      gtk_chat_context_pop(previous);
   }

   return G_SOURCE_REMOVE;
}

static void frontend_gtk_queued_print_free(gpointer opaque) {
   GtkQueuedPrint *print = opaque;
   gtk_chat_context_clear(&print->context);
   g_free(print->window);
   g_free(print->message);
   g_free(print);
}

static void frontend_gtk_vprint(const char *window, const char *fmt, va_list ap) {
   if (!fmt || dying) {
      return;
   }
   va_list copy;
   va_copy(copy, ap);
   char *rendered = g_strdup_vprintf(fmt, copy);
   va_end(copy);

   if (!frontend_gtk_event_context) {
      ui_print_gtk_literal(window, "%s", rendered);
      g_free(rendered);

      return;
   }
   GtkQueuedPrint *print = g_new0(GtkQueuedPrint, 1);
   gtk_chat_context_capture(&print->context, window);
   print->window = g_strdup(window);
   print->message = rendered;
   GSource *source = g_idle_source_new();
   /* Protocol delivery must create a JOIN/query tab before its text is drawn. */
   g_source_set_priority(source, G_PRIORITY_DEFAULT_IDLE + 1);
   g_source_set_callback(source, frontend_gtk_queued_print, print, frontend_gtk_queued_print_free);
   g_source_attach(source, frontend_gtk_event_context);
   g_source_unref(source);
}

static void frontend_gtk_vfo_widths(const char *vfo, const char *widths) {
   (void)vfo;
   (void)widths;
}

static guint frontend_gtk_update_source = 0;

static gboolean frontend_gtk_update_now(gpointer user_data) {
   (void)user_data;
   extern bool dying;
   now = time(NULL);

   if (!dying) {
      ptt_button_refresh();
   }

   if (dying) {
      // Return out of module code before the core can dlclose() this frontend.
      frontend_gtk_update_source = 0;

      if (gtk_main_level() > 0) {
         gtk_main_quit();
      }

      return G_SOURCE_REMOVE;
   }

   return G_SOURCE_CONTINUE;
}

static struct log_callback *frontend_gtk_log_callback = NULL;

/* Event-bus dispatch bridge. This context is captured on the GTK owner thread rather than looked up from whichever thread emits an event. */
typedef struct {
   event_dispatch_fn_t fn;
   void *arg;
} GtkEventDelivery;
static gboolean frontend_gtk_deliver_event(gpointer opaque) {
   GtkEventDelivery *delivery = opaque;
   frontend_gtk_assert_owner();
   delivery->fn(delivery->arg);

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_dispatch_event(event_dispatch_fn_t fn, void *arg, void *user) {
   GMainContext *context = user;
   GtkEventDelivery *delivery = g_new(GtkEventDelivery, 1);
   *delivery = (GtkEventDelivery) {
      fn, arg
   };
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_deliver_event, delivery, g_free);
   g_source_attach(source, context);
   g_source_unref(source);
}

/* Inbound snapshot from the core domain. The payload is owned by the event bus; no connection or GTK object crosses the thread boundary. */
static void frontend_gtk_authoritative_room_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || !message->length || message->length > 4096) {
      return;
   }
   const char *room = message->payload;

   if (room[message->length - 1] != '\0' || strnlen(room, message->length) != message->length - 1) {
      return;
   }
   gtk_chat_set_authoritative_room(room);
}

/* Password change is requested only after the server authenticates us and
 * explicitly marks the current credentials as requiring rotation. */
static void frontend_gtk_password_prompt_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message || !message->payload ||
       message->length != sizeof(rrclient_password_change_t)) return;
   rrclient_password_change_t req;
   memcpy(&req, message->payload, sizeof(req));
   if (!memchr(req.profile, 0, sizeof(req.profile)) ||
       !memchr(req.username, 0, sizeof(req.username))) return;
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Password change required", parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Change Password", GTK_RESPONSE_OK, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *label = gtk_label_new("The server requires a new password before you can use this station.\n"
      "Enter a new password (8-256 characters) for this account:");
   gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
   gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 8);
   GtkWidget *first = gtk_entry_new();
   GtkWidget *second = gtk_entry_new();
   gtk_entry_set_visibility(GTK_ENTRY(first), FALSE);
   gtk_entry_set_visibility(GTK_ENTRY(second), FALSE);
   gtk_entry_set_max_length(GTK_ENTRY(first), 256);
   gtk_entry_set_max_length(GTK_ENTRY(second), 256);
   gtk_entry_set_placeholder_text(GTK_ENTRY(first), "New password");
   gtk_entry_set_placeholder_text(GTK_ENTRY(second), "Confirm new password");
   gtk_box_pack_start(GTK_BOX(area), first, FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(area), second, FALSE, FALSE, 4);
   GtkWidget *error = gtk_label_new("");
   gtk_box_pack_start(GTK_BOX(area), error, FALSE, FALSE, 4);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
   gtk_widget_show_all(dialog);
   gtk_widget_grab_focus(first);
   for (;;) {
      int response = gtk_dialog_run(GTK_DIALOG(dialog));
      if (response != GTK_RESPONSE_OK) break;
      const char *p = gtk_entry_get_text(GTK_ENTRY(first));
      const char *again = gtk_entry_get_text(GTK_ENTRY(second));
      size_t length = strlen(p);
      if (length < 8 || length > 256) {
         gtk_label_set_text(GTK_LABEL(error), "Password must be 8-256 characters.");
      } else if (strcmp(p, again)) {
         gtk_label_set_text(GTK_LABEL(error), "Passwords do not match.");
      } else {
         g_strlcpy(req.password, p, sizeof(req.password));
         break;
      }
   }
   /* Reduce the lifetime of plaintext in GTK entry buffers. */
   gtk_entry_set_text(GTK_ENTRY(first), "");
   gtk_entry_set_text(GTK_ENTRY(second), "");
   gtk_widget_destroy(dialog);
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_PASSWORD_SUBMIT_EVENT, .sender = "gtk",
      .payload = &req, .length = sizeof(req)
   });
   explicit_bzero(&req, sizeof(req));
}
static void frontend_gtk_password_result_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message || !message->payload ||
       message->length != sizeof(rrclient_password_change_t)) return;
   rrclient_password_change_t result;
   memcpy(&result, message->payload, sizeof(result));
   if (!memchr(result.profile, 0, sizeof(result.profile)) ||
       !memchr(result.reason, 0, sizeof(result.reason))) return;
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
      result.success ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
      result.success ? "Password updated for %s" : "Password change failed for %s", result.profile);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "%s", result.success ? "The server confirmed the new password. It was saved in your server profile and RustyRig is reconnecting."
      : (result.reason[0] ? result.reason : "Please try again."));
   if (!result.success) {
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Retry", 1);
      gtk_widget_show_all(dialog);
      int choice = gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      if (choice == 1) {
         event_emit_message(&(rr_event_message_t) {
            .name = RRCLIENT_PASSWORD_RETRY_EVENT, .sender = "gtk",
            .payload = &result, .length = sizeof(result)
         });
      }
   } else {
      g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show(dialog);
   }
   explicit_bzero(&result, sizeof(result));
}

/* Auth errors belong to the GTK thread; never open a dialog from the
 * network/core worker.  A plain status line isn't sufficient when the
 * connection dialog has already gone away. */
static void frontend_gtk_auth_failure_response(GtkDialog *dialog, gint response, gpointer unused) {
   (void)unused;
   char *profile = g_strdup(g_object_get_data(G_OBJECT(dialog), "rr-auth-failure-profile"));
   gtk_widget_destroy(GTK_WIDGET(dialog));
   if (response == 1 && profile && *profile) {
      gtk_show_server_editor(profile);
   }
   g_free(profile);
}
static void frontend_gtk_auth_failure_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message || !message->payload ||
       message->length != sizeof(rrclient_auth_failure_alert_t)) return;
   rrclient_auth_failure_alert_t alert;
   memcpy(&alert, message->payload, sizeof(alert));
   if (!memchr(alert.profile, 0, sizeof(alert.profile)) ||
       !memchr(alert.reason, 0, sizeof(alert.reason))) return;
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(parent,
      GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
      "Authentication failed for %s", alert.profile);
   gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
   gtk_dialog_add_button(GTK_DIALOG(dialog), "_Edit Server", 1);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
   g_object_set_data_full(G_OBJECT(dialog), "rr-auth-failure-profile",
      g_strdup(alert.profile), g_free);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "%s\n\nAutomatic reconnection has been stopped. "
      "Check the username and password in Edit Server, then connect manually.",
      alert.reason);
   g_signal_connect(dialog, "response", G_CALLBACK(frontend_gtk_auth_failure_response), NULL);
   gtk_widget_show(dialog);
}

/* The TLS handshake has already failed closed. GTK alone owns the
 * confirmation dialog; core persists the pin only after the explicit reply. */
static void frontend_gtk_tls_pin_prompt_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message || !message->payload ||
       message->length != sizeof(rrclient_tls_pin_request_t)) return;
   rrclient_tls_pin_request_t request;
   memcpy(&request, message->payload, sizeof(request));
   if (!memchr(request.profile, 0, sizeof(request.profile)) ||
       !memchr(request.old_pin, 0, sizeof(request.old_pin)) ||
       !memchr(request.new_pin, 0, sizeof(request.new_pin))) return;
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;
   bool changed = request.old_pin[0] != '\0';
   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
      GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
      "%s TLS identity for %s", changed ? "Changed" : "Unknown", request.profile);
   const char *node = request.node_name[0] ? request.node_name : "(not configured)";
   const char *cn = request.cert_cn[0] ? request.cert_cn : "(not present)";
   const char *sans = request.cert_sans[0] ? request.cert_sans : "(not present)";
   const char *cert = request.cert_sha256[0] ? request.cert_sha256 : "(unavailable)";
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "Connecting to: %s\nExpected Node Name: %s\nCertificate CN: %s\n"
      "Certificate DNS SANs: %s\n\n"
      "Certificate SHA-256: %s\nPublic key SPKI SHA-256: %s\n"
      "Previously trusted SPKI: %s\n\n"
      "Station and domain are verified against the server's announcement after connection; "
      "the certificate details above are presented by the remote endpoint and are not yet trusted. "
      "Verify the key fingerprint through an independent channel before accepting.",
      request.address[0] ? request.address : "(unknown)", node, cn, sans, cert,
      request.new_pin, changed ? request.old_pin : "(none; first connection)");
   gtk_dialog_add_button(GTK_DIALOG(dialog), "_Deny", 0);
   gtk_dialog_add_button(GTK_DIALOG(dialog), "Accept _Once", 1);
   gtk_dialog_add_button(GTK_DIALOG(dialog), "Accept _Always", 2);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *instruction = gtk_label_new("For Accept Always, type YES to confirm storing this key:");
   gtk_label_set_xalign(GTK_LABEL(instruction), 0.0f);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "YES");
   gtk_box_pack_start(GTK_BOX(area), instruction, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 6);
   gtk_widget_show_all(dialog);
   gtk_widget_grab_focus(entry);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), 0);
   int response = gtk_dialog_run(GTK_DIALOG(dialog));
   request.decision = response == 1 ? 1 : response == 2 &&
      !strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "YES") ? 2 : 0;
   gtk_widget_destroy(dialog);
   event_emit_message(&(rr_event_message_t) {
      .name = RRCLIENT_TLS_PIN_REPLY_EVENT,
      .payload = &request, .length = sizeof(request)
   });
}
/* One-use bearer credential: only expose it through the explicit dialog. */
static void mesh_token_copy(GtkButton *button, gpointer user) {
   (void)button;
   GtkWidget *entry = GTK_WIDGET(user);
   GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
   gtk_clipboard_set_text(clipboard, gtk_entry_get_text(GTK_ENTRY(entry)), -1);
}
static void frontend_gtk_mesh_enrollment_token(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message || !message->payload || message->length < 2 ||
       message->length > 8192) return;
   const char *token = message->payload;
   if (token[message->length - 1] != 0 ||
       strlen(token) + 1 != message->length) return;
   GtkWindow *parent = main_window && GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Mesh enrollment invitation",
      parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 660, -1);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *hint = gtk_label_new(
      "Confidential one-time enrollment token. Copy the entire token and paste it "
      "into rrserver --mesh-join on the joining machine. Do not share it publicly.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_label_set_xalign(GTK_LABEL(hint), 0);
   gtk_box_pack_start(GTK_BOX(area), hint, FALSE, FALSE, 8);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(entry), token);
   gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
   gtk_widget_set_hexpand(entry, TRUE);
   gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 6);
   GtkWidget *copy = gtk_button_new_with_label("Copy invitation");
   gtk_box_pack_start(GTK_BOX(area), copy, FALSE, FALSE, 6);
   g_signal_connect(copy, "clicked", G_CALLBACK(mesh_token_copy), entry);
   g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
   gtk_widget_show_all(dialog);
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}
static void frontend_gtk_server_picker_show_event(const rr_event_message_t *message, void *unused) {
   (void)message;
   (void)unused;

   if (!dying) {
      gtk_show_server_chooser();
   }
}

static void frontend_gtk_ptt_state_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (message->length != 1 || !message->payload) {
      return;
   }
   const char state = ((const char *)message->payload)[0];

   if (state == '0' || state == '1') {
      g_log("RustyRig-GTK", G_LOG_LEVEL_DEBUG, "PTT state received via GTK event domain: %s", state == '1' ? "ON" : "OFF");
      ptt_button_set_state(state == '1');
   }
}

static void frontend_gtk_ptt_online_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (message->length != 1 || !message->payload) {
      return;
   }
   const char online = ((const char *)message->payload)[0];

   if (online == '0' || online == '1') {
      ptt_button_set_online(online == '1');
   }
}

static void frontend_gtk_connection_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(int)) {
      return;
   }
   int state;
   memcpy(&state, message->payload, sizeof(state));
   rr_connect_button_update_all(state);
}

static void frontend_gtk_vfo_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(rrclient_vfo_snapshot_t)) {
      return;
   }
   const rrclient_vfo_snapshot_t *snapshot = message->payload;

   /* Fixed-size snapshots contain only bounded, NUL-terminated strings. */
   if (!memchr(snapshot->vfo, 0, sizeof(snapshot->vfo)) ||
      !memchr(snapshot->mode, 0, sizeof(snapshot->mode)) ||
      !memchr(snapshot->room, 0, sizeof(snapshot->room)) || !memchr(snapshot->server, 0, sizeof(snapshot->server))) {
      return;
   }
   gtk_kiwi_panel_vfo_snapshot(snapshot);
   snprintf(pending_vfo_paint.vfo, sizeof(pending_vfo_paint.vfo), "%s", snapshot->vfo);
   snprintf(pending_vfo_paint.mode, sizeof(pending_vfo_paint.mode), "%s", snapshot->mode);
   snprintf(pending_vfo_paint.room, sizeof(pending_vfo_paint.room), "%s", snapshot->room);
   snprintf(pending_vfo_paint.server, sizeof(pending_vfo_paint.server), "%s", snapshot->server);
   pending_vfo_paint.freq = snapshot->freq;
   pending_vfo_paint.width = snapshot->width;
   pending_vfo_paint.power = snapshot->power;
   pending_vfo_paint.ptt = snapshot->ptt;
   pending_vfo_paint.has_mode = snapshot->has_mode;
   pending_vfo_paint.available = snapshot->available;

   if (!pending_vfo_paint.source) {
      pending_vfo_paint.source = g_idle_add(frontend_gtk_flush_vfo, NULL);
   }
}

static void frontend_gtk_chat_incoming_event(const rr_event_message_t *message, void *unused);
static void frontend_gtk_supervision_event(const rr_event_message_t *message, void *unused);
static void frontend_gtk_admin_result_event(const rr_event_message_t *message, void *unused);
static bool frontend_gtk_init(int *argc, char ***argv) {
   frontend_gtk_owner = g_thread_self();
   frontend_gtk_assert_owner();
   g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "GTK initialization tid=%p default-context=%p", (void *)frontend_gtk_owner, (void *)g_main_context_default());
   gtk_init(argc, argv);
   frontend_gtk_event_context = g_main_context_ref_thread_default();

   if (!event_domain_register("gtk", frontend_gtk_dispatch_event, frontend_gtk_event_context)) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to register GTK event delivery domain");
   } else {
      frontend_gtk_domain_registered = true;
   }
   gtk_identity_first_run();
#ifdef USE_LIBNOTIFY

   if (!ui_notify_init()) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Desktop notifications unavailable");
   }
#endif
   alert_dialogs_init();
   cfg_gtkcss_init();
   frontend_gtk_config_refresh(NULL);
   reload_event_add(NULL, frontend_gtk_config_refresh_cb, "refresh cached GTK settings after config reload");

   if (gui_init()) {
      return true;              // gui_init failed; module loader will unload us
   }
   gtk_clock_init();
   gtk_audio_routing_init();
   gtk_receiver_directory_init();
   gtk_connecting_init();
   gtk_resources_init();
   gtk_files_init();
   // Local client log pane (GTK log tab) mirrors client logs.
   frontend_gtk_log_callback = log_add_callback_token(log_print_va, LOG_CALLBACK_FILTERED);
   extern bool rr_module_token_add(rr_event_token_t token);
   /* Quit prompts own GTK widgets: dispatch on GTK, not on core. */
   rr_event_token_t quit_event = event_on_domain_token("client.quit.request", frontend_gtk_quit_request, NULL, "gtk");

   if (quit_event) {
      rr_module_token_add(quit_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK quit requests");
   }
   rr_event_token_t action_event = event_on_message_domain_token("client.ui.owner.action", frontend_gtk_owner_action_event, NULL, "gtk");

   if (action_event) {
      rr_module_token_add(action_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to frontend actions");
   }
   rr_event_token_t admin_event = event_on_message_domain_token("client.ui.admin.result", frontend_gtk_admin_result_event, NULL, "gtk");

   if (admin_event) {
      rr_module_token_add(admin_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to admin results");
   }
   rr_event_token_t supervision_event = event_on_message_domain_token("client.ui.supervision.result", frontend_gtk_supervision_event, NULL, "gtk");

   if (supervision_event) {
      rr_module_token_add(supervision_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to supervision results");
   }
   rr_event_token_t roster_redraw_event = event_on_message_domain_token("client.ui.roster.redraw", frontend_gtk_roster_redraw_event, NULL, "gtk");

   if (roster_redraw_event) {
      rr_module_token_add(roster_redraw_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK roster redraws");
   }
   rr_event_token_t incoming_event = event_on_message_domain_token("client.ui.chat.incoming", frontend_gtk_chat_incoming_event, NULL, "gtk");

   if (incoming_event) {
      rr_module_token_add(incoming_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to incoming chat");
   }
   rr_event_token_t room_event = event_on_message_domain_token("client.ui.chat.authoritative-room", frontend_gtk_authoritative_room_event, NULL, "gtk");

   if (room_event) {
      rr_module_token_add(room_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to authoritative room events");
   }
   rr_event_token_t password_prompt_event = event_on_message_domain_token(RRCLIENT_PASSWORD_PROMPT_EVENT,
      frontend_gtk_password_prompt_event, NULL, "gtk");
   if (password_prompt_event) rr_module_token_add(password_prompt_event);
   rr_event_token_t password_result_event = event_on_message_domain_token(RRCLIENT_PASSWORD_RESULT_EVENT,
      frontend_gtk_password_result_event, NULL, "gtk");
   if (password_result_event) rr_module_token_add(password_result_event);
   rr_event_token_t auth_failure_event = event_on_message_domain_token(RRCLIENT_AUTH_FAILURE_ALERT_EVENT,
      frontend_gtk_auth_failure_event, NULL, "gtk");
   if (auth_failure_event) rr_module_token_add(auth_failure_event);
   rr_event_token_t tls_pin_event = event_on_message_domain_token(RRCLIENT_TLS_PIN_PROMPT_EVENT, frontend_gtk_tls_pin_prompt_event, NULL, "gtk");
   if (tls_pin_event) rr_module_token_add(tls_pin_event);
   rr_event_token_t invite_event = event_on_message_domain_token("client.ui.mesh.enrollment-token",
      frontend_gtk_mesh_enrollment_token, NULL, "gtk");
   if (invite_event) rr_module_token_add(invite_event);
   rr_event_token_t picker_event = event_on_message_domain_token("client.ui.server-picker.show", frontend_gtk_server_picker_show_event, NULL, "gtk");

   if (picker_event) {
      rr_module_token_add(picker_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to server-picker requests");
   }
   gtk_chat_protocol_register(frontend_gtk_event_context);

   rr_event_token_t ptt_event = event_on_message_domain_token("client.ui.ptt.state", frontend_gtk_ptt_state_event, NULL, "gtk");

   if (ptt_event) {
      rr_module_token_add(ptt_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK PTT state updates");
   }
   rr_event_token_t ptt_online_event = event_on_message_domain_token("client.ui.ptt.online", frontend_gtk_ptt_online_event, NULL, "gtk");

   if (ptt_online_event) {
      rr_module_token_add(ptt_online_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK PTT online updates");
   }
   rr_event_token_t connection_event = event_on_message_domain_token("client.ui.connection.state", frontend_gtk_connection_event, NULL, "gtk");

   if (connection_event) {
      rr_module_token_add(connection_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK connection state");
   }
   rr_event_token_t vfo_event = event_on_message_domain_token(RRCLIENT_VFO_SNAPSHOT_EVENT, frontend_gtk_vfo_snapshot_event, NULL, "gtk");

   if (vfo_event) {
      rr_module_token_add(vfo_event);
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to VFO snapshots");
   }
   rr_event_token_t kiwi_status_event = event_on_message_domain_token(RRCLIENT_KIWI_STATUS_EVENT, gtk_kiwi_panel_status_event, NULL, "gtk");

   if (kiwi_status_event) {
      rr_module_token_add(kiwi_status_event);
   }
   rr_event_token_t local_sdr_event = event_on_message_domain_token(RRCLIENT_LOCAL_BRIDGE_ACTIVE_EVENT, gtk_kiwi_panel_active_event, NULL, "gtk");

   if (local_sdr_event) {
      rr_module_token_add(local_sdr_event);
   }
   frontend_gtk_update_source = g_timeout_add(1000, frontend_gtk_update_now, NULL);

   return false;
}

/* An orphaned GTK grab can leave the window painting while swallowing all keyboard/mouse input. Never remove live grabs (modal dialogs need them). */
static gboolean frontend_gtk_grab_watchdog(gpointer unused) {
   (void)unused;
   GtkWidget *grab = gtk_grab_get_current();

   if (!grab) {
      return G_SOURCE_CONTINUE;
   }

   if (gtk_widget_get_mapped(grab)) {
      return G_SOURCE_CONTINUE;
   }
   /* A temporarily unmapped widget may become visible in this iteration;
    * only release a grab whose widget is no longer in a visible toplevel. */
   GtkWidget *top = gtk_widget_get_toplevel(grab);

   if (gtk_widget_get_visible(grab) && GTK_IS_WINDOW(top) && gtk_widget_get_mapped(top)) {
      return G_SOURCE_CONTINUE;
   }
   g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Releasing orphaned GTK input grab: widget=%s mapped=%d visible=%d", G_OBJECT_TYPE_NAME(grab),
      gtk_widget_get_mapped(grab), gtk_widget_get_visible(grab));
   gtk_grab_remove(grab);

   return G_SOURCE_CONTINUE;
}
static void frontend_gtk_run(void) {
   frontend_gtk_assert_owner();
   guint grab_watchdog = g_timeout_add_seconds(2, frontend_gtk_grab_watchdog, NULL);
   gtk_main();
   g_source_remove(grab_watchdog);
}

static gboolean frontend_gtk_quit_on_owner(gpointer unused) {
   (void)unused;

   if (gtk_main_level() > 0) {
      gtk_main_quit();
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_quit(void) {
   if (g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_quit_on_owner(NULL);
   } else if (frontend_gtk_event_context) {
      GSource *source = g_idle_source_new();
      g_source_set_callback(source, frontend_gtk_quit_on_owner, NULL, NULL);
      g_source_attach(source, frontend_gtk_event_context);
      g_source_unref(source);
   }
}

/* Dialogs are keyed by profile so simultaneous IRC connects remain independent. */
static GHashTable *irc_nick_dialogs;

static void irc_nick_dialog_destroy(GtkWidget *dialog, gpointer data) {
   (void)data;

   if (irc_nick_dialogs) {
      g_hash_table_remove(irc_nick_dialogs, g_object_get_data(G_OBJECT(dialog), "profile"));
   }
}

static void irc_nick_dialog_response(GtkDialog *dialog, gint response, gpointer data) {
   (void)data;

   if (response == GTK_RESPONSE_OK) {
      GtkEntry *entry = g_object_get_data(G_OBJECT(dialog), "nickname");
      const char *profile = g_object_get_data(G_OBJECT(dialog), "profile");

      if (!rrclient_irc_nick_submit(profile, gtk_entry_get_text(entry))) {
         gtk_widget_set_tooltip_text(GTK_WIDGET(entry), "Enter a valid IRC nickname");

         return;
      }
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void frontend_gtk_irc_nick_prompt_owner(const char *profile, const char *nickname) {
   if (!irc_nick_dialogs) {
      irc_nick_dialogs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   }
   GtkWidget *existing = g_hash_table_lookup(irc_nick_dialogs, profile);

   if (existing) {
      gtk_widget_destroy(existing);
   }

   if (!nickname) {
      return;
   }
   gui_window_t *main = gui_find_window(NULL, "main");
   GtkWidget *dialog = gtk_dialog_new_with_buttons("IRC nickname already in use", main ? GTK_WINDOW(main->gtk_win) : NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
      "Wait for suffix", GTK_RESPONSE_CANCEL, "Use nickname", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char text[512];
   snprintf(text, sizeof(text), "%s: %s is already in use. Enter another nickname.\nAfter 45 seconds, automatic suffix retries begin.", profile, nickname);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(text), FALSE, FALSE, 6);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(entry), NICKLEN);
   gtk_entry_set_text(GTK_ENTRY(entry), nickname);
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 6);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   g_object_set_data_full(G_OBJECT(dialog), "profile", g_strdup(profile), g_free);
   g_object_set_data(G_OBJECT(dialog), "nickname", entry);
   g_hash_table_insert(irc_nick_dialogs, g_strdup(profile), dialog);
   g_signal_connect(dialog, "response", G_CALLBACK(irc_nick_dialog_response), NULL);
   g_signal_connect(dialog, "destroy", G_CALLBACK(irc_nick_dialog_destroy), NULL);
   gtk_widget_show_all(dialog);
}

/* IRC nickname requests can arrive from protocol/core callbacks. */
typedef struct {
   char *profile, *nickname;
} GtkNickPrompt;
static gboolean frontend_gtk_nick_prompt_deliver(gpointer opaque) {
   GtkNickPrompt *p = opaque;

   if (!dying) {
      frontend_gtk_irc_nick_prompt_owner(p->profile, p->nickname);
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_nick_prompt_free(gpointer opaque) {
   GtkNickPrompt *p = opaque;
   g_free(p->profile);
   g_free(p->nickname);
   g_free(p);
}
static void frontend_gtk_irc_nick_prompt(const char *profile, const char *nickname) {
   if (!profile) {
      return;
   }
   GtkNickPrompt *p = g_new0(GtkNickPrompt, 1);
   p->profile = g_strdup(profile);
   p->nickname = g_strdup(nickname);

   if (g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_nick_prompt_deliver(p);
      frontend_gtk_nick_prompt_free(p);
   } else if (frontend_gtk_event_context) {
      GSource *source = g_idle_source_new();
      g_source_set_callback(source, frontend_gtk_nick_prompt_deliver, p, frontend_gtk_nick_prompt_free);
      g_source_attach(source, frontend_gtk_event_context);
      g_source_unref(source);
   } else {
      frontend_gtk_nick_prompt_free(p);
   }
}

/* The event bus owns the cross-thread message copy and GTK dispatch. */
typedef struct {
   uint32_t room_len, text_len, nick_len;
   uint8_t query;
} GtkIncomingChatHeader;
static void frontend_gtk_chat_incoming_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (dying || !message->payload || message->length < sizeof(GtkIncomingChatHeader)) {
      return;
   }
   GtkIncomingChatHeader header;
   memcpy(&header, message->payload, sizeof(header));
   size_t remaining = message->length - sizeof(header);
   size_t required = (size_t)header.room_len + header.text_len + header.nick_len;

   if (required != remaining || header.room_len == 0 || header.text_len == 0 || header.nick_len == 0) {
      return;
   }
   const char *p = (const char *)message->payload + sizeof(header);
   const char *room = p;
   const char *text = room + header.room_len;
   const char *nickname = text + header.text_len;

   if (room[header.room_len - 1] || text[header.text_len - 1] || nickname[header.nick_len - 1]) {
      return;
   }
   gtk_chat_incoming(*room ? room : NULL, header.query != 0, *text ? text : NULL, *nickname ? nickname : NULL);
}
static void frontend_gtk_chat_incoming(const char *room, bool query, const char *text, const char *nickname) {
   size_t rl = strlen(room ? room : "") + 1, tl = strlen(text ? text : "") + 1;
   size_t nl = strlen(nickname ? nickname : "") + 1;

   if (rl > UINT32_MAX || tl > UINT32_MAX || nl > UINT32_MAX || rl + tl + nl > 1024 * 1024) {
      return;
   }
   GtkIncomingChatHeader header = {
      (uint32_t) rl, (uint32_t) tl, (uint32_t) nl, query
   };
   size_t length = sizeof(header) + rl + tl + nl;
   char *payload = g_malloc(length);
   memcpy(payload, &header, sizeof(header));
   char *p = payload + sizeof(header);
   memcpy(p, room ? room : "", rl);
   p += rl;
   memcpy(p, text ? text : "", tl);
   p += tl;
   memcpy(p, nickname ? nickname : "", nl);
   rr_event_message_t message = {
      .name = "client.ui.chat.incoming",
      .payload = payload, .length = length
   };
   event_emit_message(&message);
   g_free(payload);
}

/* Synchronous frontend getter: never inspect GTK from core. The command callers should eventually consume a core-owned UI state snapshot. */
static bool frontend_gtk_chat_status_active(void) {
   if (g_thread_self() != frontend_gtk_owner) {
      return false;
   }

   return gtk_chat_status_active();
}

extern void gtk_supervision_result_profile(const char *, const char *);
/* Two copied strings; never ship rrconn_t pointers to GTK. */
typedef struct {
   uint32_t profile_len, data_len;
} GtkSupervisionMessage;
static void frontend_gtk_supervision_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (dying || !message->payload || message->length < sizeof(GtkSupervisionMessage)) {
      return;
   }
   GtkSupervisionMessage header;
   memcpy(&header, message->payload, sizeof(header));
   size_t available = message->length - sizeof(header);

   if (!header.profile_len || !header.data_len || header.profile_len > available || header.data_len != available - header.profile_len) {
      return;
   }
   const char *profile = (const char *)message->payload + sizeof(header);
   const char *data = profile + header.profile_len;

   if (profile[header.profile_len - 1] || data[header.data_len - 1]) {
      return;
   }
   gtk_supervision_result_profile(data, profile);
}
static void frontend_gtk_supervision_result(const char *data, rrconn_t *connection) {
   const char *profile = rrclient_connection_name(connection);

   if (!profile || !*profile) {
      return;
   }
   size_t pl = strlen(profile) + 1, dl = strlen(data ? data : "") + 1;

   if (pl > 4096 || dl > 1024 * 1024) {
      return;
   }
   GtkSupervisionMessage header = {
      (uint32_t) pl, (uint32_t) dl
   };
   size_t length = sizeof(header) + pl + dl;
   char *payload = g_malloc(length);
   memcpy(payload, &header, sizeof(header));
   memcpy(payload + sizeof(header), profile, pl);
   memcpy(payload + sizeof(header) + pl, data ? data : "", dl);
   rr_event_message_t message = {
      .name = "client.ui.supervision.result", .payload = payload, .length = length
   };
   event_emit_message(&message);
   g_free(payload);
}

/* Admin result callbacks originate in core; snapshot profile + response first. */
typedef struct {
   uint32_t profile_len, data_len;
} GtkAdminResultHeader;
static void frontend_gtk_admin_result_event(const rr_event_message_t *message, void *unused);
extern void gtk_admin_result_profile(const char *, const char *);
static void frontend_gtk_admin_result(const char *data, rrconn_t *connection) {
   const char *profile = rrclient_connection_name(connection);

   if (!profile || !*profile) {
      return;
   }
   size_t pl = strlen(profile) + 1, dl = strlen(data ? data : "") + 1;

   if (pl > 4096 || dl > 1024 * 1024) {
      return;
   }
   GtkAdminResultHeader h = {
      (uint32_t) pl, (uint32_t) dl
   };
   size_t length = sizeof(h) + pl + dl;
   char *payload = g_malloc(length);
   memcpy(payload, &h, sizeof(h));
   memcpy(payload + sizeof(h), profile, pl);
   memcpy(payload + sizeof(h) + pl, data ? data : "", dl);
   rr_event_message_t event = {
      .name = "client.ui.admin.result", .payload = payload, .length = length
   };
   event_emit_message(&event);
   g_free(payload);
}
static void frontend_gtk_admin_result_event(const rr_event_message_t *event, void *unused) {
   (void)unused;

   if (dying || !event->payload || event->length < sizeof(GtkAdminResultHeader)) {
      return;
   }
   GtkAdminResultHeader h;
   memcpy(&h, event->payload, sizeof(h));
   size_t remaining = event->length - sizeof(h);

   if (!h.profile_len || !h.data_len || h.profile_len > remaining || h.data_len != remaining - h.profile_len) {
      return;
   }
   const char *profile = (const char *)event->payload + sizeof(h);
   const char *data = profile + h.profile_len;

   if (profile[h.profile_len - 1] || data[h.data_len - 1]) {
      return;
   }
   gtk_admin_result_profile(data, profile);
}


const rr_frontend_ops_t gtk_frontend_ops = {
   .supervision_result = frontend_gtk_supervision_result,
   .admin_result = frontend_gtk_admin_result,
   .irc_nick_prompt = frontend_gtk_irc_nick_prompt,
   .name = "gtk",
   .init = frontend_gtk_init,
   .run = frontend_gtk_run,
   .quit = frontend_gtk_quit,
   .stop = rr_module_frontend_stop,
   .vfo_state = frontend_gtk_vfo_state,
   .vfo_widths = frontend_gtk_vfo_widths,
   .freq_set = frontend_gtk_freq_set,
   .mode_set = frontend_gtk_mode_set,
   .connection_state = frontend_gtk_conn_button,
   .conn_button_update = frontend_gtk_conn_button,
   .chat_incoming = frontend_gtk_chat_incoming,
   .chat_room_add = frontend_gtk_chat_room_add,
   .chat_room_remove = frontend_gtk_chat_room_remove,
   .chat_room_topic = frontend_gtk_chat_room_topic,
   .chat_show_status = frontend_gtk_chat_show_status,
   .chat_set_authoritative_room = frontend_gtk_chat_set_authoritative_room,
   .chat_room_vfos_changed = frontend_gtk_chat_room_vfos_changed,
   .chat_current_room = frontend_gtk_chat_current_room,
   .chat_status_active = frontend_gtk_chat_status_active,
   .chat_query_add = frontend_gtk_chat_query_add,
   .ptt_set_online = frontend_gtk_ptt_set_online,
   .ptt_set_state = frontend_gtk_ptt_set_state,
   .ptt_tot_expired = frontend_gtk_ptt_tot_expired,
   .ptt_refresh = frontend_gtk_ptt_refresh,
   .ptt_hotkey_toggle = frontend_gtk_ptt_hotkey_toggle,
   .codec_set_active = frontend_gtk_codec_set_active,
   .syslog_clear = frontend_gtk_syslog_clear,
   .focus_tab = frontend_gtk_focus_tab,
   .switch_window = frontend_gtk_switch_window,
   .confirm_dialog = frontend_gtk_confirm_dialog,
   .edit_config = frontend_gtk_edit_config,
   .alert = frontend_gtk_alert,
   .bell = frontend_gtk_bell,
   .notify = frontend_gtk_notify,
   .show_server_chooser = frontend_gtk_show_server_chooser,
   .webcam_show = frontend_gtk_webcam_show,
   .room_state_changed = frontend_gtk_room_state_changed,
   .userlist_redraw = frontend_gtk_userlist_redraw,
   .userlist_set_visible = frontend_gtk_userlist_set_visible,
   .userlist_room_vfos_changed = frontend_gtk_userlist_room_vfos_changed,
   .chat_clear = frontend_gtk_chat_clear,
   .rx_volume = frontend_gtk_rx_volume,
   .vprint = frontend_gtk_vprint,
};

void gtk_frontend_stop(void) {
   if (frontend_gtk_stopped) {
      return;
   }
   frontend_gtk_assert_owner();
   frontend_gtk_stopped = true;
   extern void gtk_terminal_shutdown(void);
   gtk_terminal_shutdown();
   gtk_help_shutdown();
   gtk_resources_shutdown();
   gtk_files_shutdown();
   gtk_file_editor_shutdown();
   gtk_receiver_directory_shutdown();
   gtk_connecting_shutdown();
   gtk_clock_shutdown();
   gtk_audio_routing_shutdown();
   gtk_ui_zoom_shutdown();

   // No logger callback may point into this module after dlclose().
   if (frontend_gtk_log_callback) {
      log_remove_callback(frontend_gtk_log_callback);
      frontend_gtk_log_callback = NULL;
   }
   // Stop the 1hz timer and quit the main loop if running. The main window
   // "destroy" signal handler runs gtk_main_quit when the user closes the
   // window; this path is for shutdown initiated from the core.
   extern void gtk_userlist_stop_timers(void);  // gtk.userlist.c

   if (frontend_gtk_update_source) {
      g_source_remove(frontend_gtk_update_source);
      frontend_gtk_update_source = 0;
   }

   if (frontend_gtk_roster_source) {
      g_source_remove(frontend_gtk_roster_source);
      frontend_gtk_roster_source = 0;
   }

   if (pending_vfo_paint.source) {
      g_source_remove(pending_vfo_paint.source);
      pending_vfo_paint.source = 0;
   }
   extern void gtk_chat_typing_stop_timers(void);
   gtk_chat_typing_stop_timers();
   gtk_userlist_stop_timers();
   extern void gtk_winmgr_stop_sources(void);   // gtk.winmgr.c
   gtk_winmgr_stop_sources();

   if (main_window && GTK_IS_WINDOW(main_window)) {
      gtk_widget_destroy(main_window);
      main_window = NULL;
   }

   if (gtk_main_level() > 0) {
      gtk_main_quit();
   }
}

bool gtk_frontend_events_shutdown(void) {
   if (frontend_gtk_domain_registered) {
      if (!event_domain_unregister("gtk")) {
         return false;
      }
      frontend_gtk_domain_registered = false;
   }

   if (frontend_gtk_event_context) {
      g_main_context_unref(frontend_gtk_event_context);
      frontend_gtk_event_context = NULL;
   }

   return true;
}

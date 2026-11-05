#include "gtk.chat-context.h"
#include <rrclient/config.layers.h>
#include <rrclient/database.h>
//
// rrclient/gtk/gtk.core.c: Core of GTK gui
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
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <librrprotocol/crypto.h>
#include <rrclient/identity.store.h>
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
#include "gtk.identity.h"
#include "gtk.screensaver.h"
#include <rrclient/gtk/gtk.connecting.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include <rrclient/gtk/gtk.connect-button.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include <rrclient/gtk/gtk.audio-routing.h>
#include <rrclient/gtk/gtk.receiver-directory.h>
#include <rrclient/gtk/gtk.resources.h>
#include <rrclient/gtk/gtk.files.h>
#include <rrclient/gtk/gtk.file-editor.h>
#include <rrclient/gtk/gtk.tabs.h>
#include <rrclient/gtk/gtk.help.h>

#define MSGBUF_SIZE 8096

extern dict *cfg;
extern time_t now;
extern bool dying;              // main.c
/* Width diagnostic: intentionally read-only. GTK's nested viewports can conceal a child wider than the visible client area. */
static void main_width_diagnostic(GtkWidget *content, GtkAllocation *allocation, gpointer unused) {
   (void)unused;
   GtkWidget *toolbar = g_object_get_data(G_OBJECT(content), "rr-width-toolbar");
   GtkWidget *tabs = g_object_get_data(G_OBJECT(content), "rr-width-tabs");

   if (!toolbar || !tabs) {
      return;
   }
   gint toolbar_min = 0, toolbar_nat = 0, tabs_min = 0, tabs_nat = 0;
   gtk_widget_get_preferred_width(toolbar, &toolbar_min, &toolbar_nat);
   gtk_widget_get_preferred_width(tabs, &tabs_min, &tabs_nat);
   gint toolbar_alloc = gtk_widget_get_allocated_width(toolbar);
   gint tabs_alloc = gtk_widget_get_allocated_width(tabs);
   gint available = allocation->width;
   static gint last_available = -1, last_toolbar = -1, last_tabs = -1;

   if (available == last_available && toolbar_alloc == last_toolbar && tabs_alloc == last_tabs) {
      return;
   }
   last_available = available;
   last_toolbar = toolbar_alloc;
   last_tabs = tabs_alloc;

   if (toolbar_min > available + 8 || tabs_min > available + 8 || toolbar_alloc > available + 8 || tabs_alloc > available + 8) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "content=%d toolbar(min=%d nat=%d alloc=%d) tabs(min=%d nat=%d alloc=%d)", available, toolbar_min, toolbar_nat,
         toolbar_alloc, tabs_min, tabs_nat, tabs_alloc);
   }
}

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
static int cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM; // module-local
extern bool cfg_ui_gtk_vfo_on_top;  // Defined by the rrclient host.
extern bool cfg_fullscreen;

static void frontend_gtk_config_refresh(const char *key) {
   (void)key;
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);
   g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Effective startup setting ui.full-screen=%s (raw=%s)", cfg_fullscreen ? "true" : "false", cfg_get(
      "ui.full-screen") ? cfg_get("ui.full-screen") : "(absent)");
   cfg_ui_gtk_vfo_on_top = cfg_get_bool("ui.gtk.vfo-on-top", true);
   const char *tabstrip = cfg_get("ui.gtk.main-tabstrip");

   if (tabstrip && strcasecmp(tabstrip, "left") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_LEFT;
   } else if (tabstrip && strcasecmp(tabstrip, "right") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_RIGHT;
   } else if (tabstrip && strcasecmp(tabstrip, "top") == 0) {
      cfg_ui_gtk_main_tabstrip = GTK_POS_TOP;
   } else {
      cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM;
   }
}

static bool frontend_gtk_config_refresh_cb(const char *key) {
   frontend_gtk_config_refresh(key);

   if (main_window) {
      gtk_css_apply_cfg();
      gtk_ui_zoom_recheck();
   }

   return true;
}

extern GtkWidget *init_admin_tab(void);
extern bool chat_init(void);    // gtk.chat.c
bool cfg_fullscreen = false;

// Trim a text buffer to the configured scrollback length.
// cfg_key: config key holding the max line count (0 = unlimited)
// def: fallback if the key isn't set.
// Counting lines isn't free, so we only check every 16 inserts.
static int scrollback_skip = 0;

// Lines printed while the chat tab was hidden. They're rendered through the
// normal markup path once the tab is focused again, so colors are preserved.
static GQueue *chat_backlog = NULL;

// Cap on the hidden-tab backlog so a long time away can't grow it unbounded.
// Old lines beyond the cap are dropped, matching the scrollback trim.
#define CHAT_BACKLOG_MAX 200

extern GtkWidget *main_notebook; // gtk.chat.c / gtk.core.c
extern GtkWidget *status_tab;   // gtk.chat.c: the chat tab page widget

// Queue a raw line before the status buffer exists
static void chat_backlog_push(const char *line) {
   if (!line) {
      return;
   }

   if (!chat_backlog) {
      chat_backlog = g_queue_new();
   }

   char *copy = strdup(line);

   if (!copy) {
      return;
   }
   g_queue_push_tail(chat_backlog, copy);

   while (g_queue_get_length(chat_backlog) > CHAT_BACKLOG_MAX) {
      g_free(g_queue_pop_head(chat_backlog));
   }
}

// Render any queued lines into the chat buffer with the normal markup path.
// Early startup messages are flushed into the dedicated status buffer.
static void chat_backlog_flush(GtkTextBuffer *buffer, GtkWidget *view) {
   if (!chat_backlog || g_queue_is_empty(chat_backlog) || !buffer) {
      return;
   }

   GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
   bool follow = gtk_adjustment_get_value(adjustment) + gtk_adjustment_get_page_size(adjustment) >= gtk_adjustment_get_upper(adjustment) - 1;
   char *line;

   while ((line = g_queue_pop_head(chat_backlog))) {
      char *colorized = gtk_colorize_string(line);

      GtkTextIter end;

      gtk_text_buffer_get_end_iter(buffer, &end);

      if (colorized) {
         gtk_text_buffer_insert_markup(buffer, &end, colorized, -1);
         g_free(colorized);
      } else {
         gtk_text_buffer_insert(buffer, &end, line, -1);
      }
      gtk_text_buffer_insert(buffer, &end, "\n", 1);
      g_free(line);
   }
   gtk_trim_scrollback(buffer, "ui.gtk.scrollback.chat", 200);

   if (follow) {
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, ui_scroll_to_end, g_object_ref(view), g_object_unref);
   }
}

void gtk_trim_scrollback(GtkTextBuffer *buf, const char *cfg_key, int def) {
   if (!buf || !cfg_key) {
      return;
   }

   // Counting lines isn't free; only do it every 16 inserts
   if (++scrollback_skip < 16) {
      return;
   }
   scrollback_skip = 0;

   int max = cfg_get_int(cfg_key, def);

   if (max <= 0) {
      return;                   // unlimited
   }

   int count = gtk_text_buffer_get_line_count(buf);

   if (count <= max) {
      return;
   }

   GtkTextIter start, end;
   gtk_text_buffer_get_start_iter(buf, &start);
   gtk_text_buffer_get_iter_at_line(buf, &end, count - max);
   gtk_text_buffer_delete(buf, &start, &end);
}

bool ui_print_gtk(const char *window, const char *fmt, va_list ap) {
   if (!fmt) {
      return true;
   }

   // During shutdown the chat text buffer is destroyed before the network
   // layer finishes tearing down; don't touch GTK widgets once dying.
   if (dying) {
      return true;
   }

   char msgbuf[MSGBUF_SIZE];
   memset(msgbuf, 0, sizeof(msgbuf));

   va_list aq;
   va_copy(aq, ap);
   vsnprintf(msgbuf, sizeof(msgbuf), fmt, aq);
   va_end(aq);

   /* Add timestamps to local notices and command output too. Chat/relay messages already start with get_chat_ts()'s colored "[...]" prefix. */
   if (strncmp(msgbuf, "\00314[", 4) != 0) {
      char stamped[sizeof(msgbuf)];
      const char *stamp = get_chat_ts(time(NULL));
      snprintf(stamped, sizeof(stamped), "%s%s", stamp ? stamp : "", msgbuf);
      snprintf(msgbuf, sizeof(msgbuf), "%s", stamped);
   }

   /* NULL/status output belongs to the persistent status tab, regardless of which room is selected. Keep early startup messages until it exists. */
   GtkTextBuffer *target_buffer = NULL;
   GtkWidget *target_view = NULL;
   bool explicit_room = window && *window && strcasecmp(window, "status");

   if (!gtk_chat_room_widgets(window, &target_buffer, &target_view)) {
      explicit_room = false;
      gtk_chat_room_widgets(NULL, &target_buffer, &target_view);
   }

   if (!target_buffer || !target_view || !GTK_IS_TEXT_VIEW(target_view)) {
      chat_backlog_push(msgbuf);

      return false;
   }

   if (!explicit_room) {
      chat_backlog_flush(target_buffer, target_view);
   }

   /* PARITY: rustyrig-www/js/webui.chat.js Append, preserve a reader's viewport. */
   GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(target_view));
   bool follow = gtk_adjustment_get_value(adjustment) + gtk_adjustment_get_page_size(adjustment) >= gtk_adjustment_get_upper(adjustment) - 1;
   bool colorize_failed = false;
   char *colorized = gtk_colorize_string(msgbuf);

   if (!colorized) {
      colorize_failed = true;
      colorized = msgbuf;
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "ui_print_gtk: gtk_colorize_string failed");
   }

   GtkTextIter end;

   gtk_text_buffer_get_end_iter(target_buffer, &end);
   int start_offset = gtk_text_iter_get_offset(&end);
   gtk_text_buffer_insert_markup(target_buffer, &end, colorized, -1);
   gtk_chat_highlight_prefix(target_view, start_offset);
   /* Markup insertion and prefix highlighting may invalidate end. Do not reuse an iterator across mutations of indexable buffer content. */
   gtk_text_buffer_get_end_iter(target_buffer, &end);
   gtk_text_buffer_insert(target_buffer, &end, "\n", 1);
   gtk_trim_scrollback(target_buffer, "ui.gtk.scrollback.chat", 200);

   if (!colorize_failed) {
      g_free(colorized);
   }

   if (follow) {
      g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, ui_scroll_to_end, g_object_ref(target_view), g_object_unref);
   }

   return false;
}

void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text) {
   if (!combo || !text) {
      return;
   }
   GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
   GtkTreeIter iter;
   int index = 0;

   if (gtk_tree_model_get_iter_first(model, &iter)) {
      do {
         gchar *str = NULL;
         gtk_tree_model_get(model, &iter, 0, &str, -1);

         if (str && strcasecmp(str, text) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_free(str);

            return;
         }
         g_free(str);
         index++;
      } while (gtk_tree_model_iter_next(model, &iter));
   }
}

void update_connection_button(int connected, GtkWidget *btn) {
   if (!btn) {
      return;
   }
   GtkStyleContext *ctx = gtk_widget_get_style_context(btn);

   if (!ctx) {
      return;
   }

   if (connected == 1) {
      gtk_button_set_label(GTK_BUTTON(btn), "Online");
      gtk_style_context_remove_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-pending");
      gtk_style_context_add_class(ctx, "conn-active");
   } else if (connected == 0) {
      gtk_button_set_label(GTK_BUTTON(btn), "Offline");
      gtk_style_context_add_class(ctx, "conn-idle");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-pending");
   } else if (connected == -1) {
      gtk_button_set_label(GTK_BUTTON(btn), "WAIT");
      gtk_style_context_add_class(ctx, "conn-pending");
      gtk_style_context_remove_class(ctx, "conn-active");
      gtk_style_context_remove_class(ctx, "conn-idle");
   }
}

static gboolean fullscreen_later(gpointer data) {
   (void)data;

   /* cfg_fullscreen is already the requested initial state; toggling here used to unfullscreen the window and flip the setting to false. */
   if (main_window && cfg_fullscreen) {
      gtk_window_fullscreen(GTK_WINDOW(main_window));
      gtk_ui_zoom_recheck();
   }

   return G_SOURCE_REMOVE;
}

/* Report the state the window manager actually applied, not just our fullscreen request. Run once after startup for release diagnostics. */
static gboolean fullscreen_startup_check(gpointer unused) {
   (void)unused;

   if (!main_window || !gtk_widget_get_window(main_window)) {
      return G_SOURCE_REMOVE;
   }
   GdkWindowState state = gdk_window_get_state(gtk_widget_get_window(main_window));
   bool actual = (state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
   g_log("RustyRig-GTK", cfg_fullscreen
      && !actual ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE, "Startup window state: requested_fullscreen=%s actual_fullscreen=%s maximized=%s", cfg_fullscreen
      ? "yes" : "no", actual ? "yes" : "no", (state & GDK_WINDOW_STATE_MAXIMIZED) ? "yes" : "no");

   return G_SOURCE_REMOVE;
}

static gboolean on_focus_in(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
   if (!widget) {
      return FALSE;
   }
   gtk_window_set_urgency_hint(GTK_WINDOW(widget), FALSE);

   return FALSE;
}

// Map plain Y/N (and y/n) keys to the dialog's Yes/No responses
static gboolean on_confirm_dialog_key(GtkWidget *widget, GdkEventKey *ev, gpointer data) {
   if (!widget || !ev) {
      return FALSE;
   }

   switch (ev->keyval) {
      case 'y':
      case 'Y': {
         gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_YES);

         return TRUE;
      }

      case 'n':
      case 'N': {
         gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_NO);

         return TRUE;
      }
   }

   return FALSE;
}

bool ui_confirm_dialog(GtkWindow *parent, const char *message) {
   if (!message) {
      return false;
   }

   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", message);
   /* Keep confirmation prompts in the middle of the display even when the parent is a tab or a partially off-screen window. */
   gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
   /* Quit confirmation must remain accessible even if another utility dialog has an always-on-top hint. Transient/modal alone cannot guarantee that. */
   gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
   gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
   /* Keep the confirmation above independent always-on-top floating windows. Some WMs do not rank two keep-above windows according to transient order.
    * Temporarily suspend those hints for the lifetime of the modal prompt. */
   GList *top_windows = gtk_window_list_toplevels();
   GList *demoted = NULL;

   for (GList *it = top_windows ; it ; it = it->next) {
      GtkWidget *window = it->data;
      GdkWindow *gdk_window = GTK_IS_WINDOW(window) ? gtk_widget_get_window(window) : NULL;

      if (window != dialog && gdk_window && gtk_widget_get_visible(window) && (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_ABOVE)) {
         gtk_window_set_keep_above(GTK_WINDOW(window), FALSE);
         demoted = g_list_prepend(demoted, g_object_ref(window));
      }
   }

   g_list_free(top_windows);
   g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_confirm_dialog_key), NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
   gtk_widget_show(dialog);
   gtk_window_present(GTK_WINDOW(dialog));
   gboolean confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);

   for (GList *it = demoted ; it ; it = it->next) {
      GtkWidget *window = it->data;

      if (GTK_IS_WINDOW(window)) {
         gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
      }
      g_object_unref(window);
   }

   g_list_free(demoted);

   return confirmed;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;
   event_emit("client.quit.request", NULL, NULL);

   return TRUE;                 // core handles shutdown after GTK confirmation
}

static GtkWidget *rr_client_overlay;
static void main_toolbar_minimize(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;

   if (main_window) {
      gtk_window_iconify(GTK_WINDOW(main_window));
   }
}

static void main_toolbar_quit(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   event_emit("client.quit.request", NULL, NULL);
}


static void frontend_gtk_main_window_destroy(GtkWidget *widget, gpointer unused) {
   (void)unused;

   if (main_window == widget) {
      main_window = NULL;
   }

   if (gtk_main_level() > 0) {
      gtk_main_quit();
   }
}

bool gui_init(void) {
   // Apply user CSS (from [gtk-css] config section) or the default from
   // defconfig.c. See cfg.gtkcss.c, reloadable at runtime with /css-reload
   gtk_css_apply_cfg();

   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *main_window_t = ui_new_window(main_window, "main");
   gtk_window_set_title(GTK_WINDOW(main_window), "rustyrig remote client");

   // Apply the default UI font to the whole window tree. Widgets which set
   // their own font (chat, frequency entry, etc) keep it; everything else
   // (labels, buttons, tabs, ...) inherits this one.
   // Attach the notebook to the main window for tabs
   main_notebook = gtk_notebook_new();
   /* The shared status/command toolbar belongs above every notebook page. */
   GtkWidget *main_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   rr_main_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
   gtk_widget_set_hexpand(rr_main_toolbar, TRUE);
   gtk_widget_set_margin_start(rr_main_toolbar, 4);
   GtkWidget *minimize = gtk_button_new_from_icon_name("window-minimize-symbolic", GTK_ICON_SIZE_MENU);
   gtk_button_set_relief(GTK_BUTTON(minimize), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(minimize, "Minimize the main window (VFO locator stays open)");
   g_signal_connect(minimize, "clicked", G_CALLBACK(main_toolbar_minimize), NULL);
   gtk_box_pack_start(GTK_BOX(rr_main_toolbar), minimize, FALSE, FALSE, 0);
   GtkWidget *quit_button = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_relief(GTK_BUTTON(quit_button), GTK_RELIEF_NONE);
   gtk_widget_set_tooltip_text(quit_button, "Quit RustyRig");
   g_signal_connect(quit_button, "clicked", G_CALLBACK(main_toolbar_quit), NULL);
   gtk_box_pack_end(GTK_BOX(rr_main_toolbar), quit_button, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(main_content), rr_main_toolbar, FALSE, TRUE, 1);
   GtkWidget *tabs_wrapper = gtk_wrapping_tabs_new(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);
   gtk_box_pack_start(GTK_BOX(main_content), tabs_wrapper, TRUE, TRUE, 0);

   /* Preferred-width queries inside size-allocate can force nested GTK layout recalculation on every resize/focus transition. Keep this diagnostic opt-in;
    * never run it on the normal GUI hot path. */
   if (g_strcmp0(g_getenv("RRCLIENT_GTK_WIDTH_DIAGNOSTIC"), "1") == 0) {
      g_object_set_data(G_OBJECT(main_content), "rr-width-toolbar", rr_main_toolbar);
      g_object_set_data(G_OBJECT(main_content), "rr-width-tabs", tabs_wrapper);
      g_signal_connect(main_content, "size-allocate", G_CALLBACK(main_width_diagnostic), NULL);
   }
   gtk_ui_zoom_attach(main_window, main_content);
   /* gtk_ui_zoom_attach installs the viewport as the window child. Reparent it into an overlay so the screensaver covers the whole content area. */
   GtkWidget *viewport = gtk_bin_get_child(GTK_BIN(main_window));
   g_object_ref(viewport);
   gtk_container_remove(GTK_CONTAINER(main_window), viewport);
   rr_client_overlay = gtk_overlay_new();
   gtk_widget_set_hexpand(rr_client_overlay, TRUE);
   gtk_widget_set_vexpand(rr_client_overlay, TRUE);
   gtk_container_add(GTK_CONTAINER(main_window), rr_client_overlay);
   gtk_container_add(GTK_CONTAINER(rr_client_overlay), viewport);
   GtkWidget *tab_strip = g_object_get_data(G_OBJECT(main_notebook), "rr-tab-strip-widget");
   GtkWidget *upper_extra = cfg_ui_gtk_main_tabstrip == GTK_POS_TOP ? tab_strip : NULL;
   GtkWidget *lower = cfg_ui_gtk_main_tabstrip == GTK_POS_BOTTOM ? tab_strip : NULL;
   rr_floating_panel_set_reserved_widgets(GTK_OVERLAY(rr_client_overlay), rr_main_toolbar, upper_extra, lower, RR_FLOATING_PANEL_SCROLLBAR_INSET);
   g_object_unref(viewport);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(gtk_screensaver_key_press), NULL);
   g_signal_connect(main_window, "event", G_CALLBACK(gtk_screensaver_activity_event), NULL);
   gtk_screensaver_activity();
   g_timeout_add_seconds(1, gtk_screensaver_idle_check, NULL);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), FALSE);
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);

   // ADMIN tab (alt-1)
   admin_tab = init_admin_tab();

   // CONFIG tab (alt-2)
   config_tab = init_config_tab();
   // One GTK Log tab combines Client and Host streams; TUI tabs stay separate.
   log_tab = init_log_tab();
   host_log_tab = log_tab;

   //// CHAT stuff (alt-5+)...
   chat_init();

   // Shared serial console page, with top-aligned endpoint subtabs.
   extern GtkWidget *init_terminal_tab(void);
   init_terminal_tab();

   // GTK Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(frontend_gtk_main_window_destroy), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
   g_signal_connect(main_window, "delete-event", G_CALLBACK(on_window_delete), NULL);

   // bind our hotkeys
   gui_hotkey_register(main_window);

   // Video (webcam) frame listener
   extern void rrclient_webcam_register(void);  // gtk.webcam.c
   rrclient_webcam_register();

   /* Ask GTK for fullscreen before the window is mapped. An idle callback after show_all() can lose the race against the initial WM placement. */
   if (cfg_fullscreen) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Startup requests fullscreen (ui.full-screen=true)");
      gtk_window_fullscreen(GTK_WINDOW(main_window));
   } else {
      g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Startup requests normal window (ui.full-screen=false)");
   }

   // Make the main window on screen
   gtk_widget_show_all(main_window);
   gtk_widget_realize(main_window);
   place_window(main_window);

   /* Never restore an oversized normal window beyond its monitor workarea. The zoom layer can then shrink the 100% layout to the available size. */
   if (!cfg_fullscreen) {
      GdkWindow *gdk_window = gtk_widget_get_window(main_window);
      GdkDisplay *display = gtk_widget_get_display(main_window);
      GdkMonitor *monitor = gdk_window ? gdk_display_get_monitor_at_window(display, gdk_window) : gdk_display_get_primary_monitor(display);

      if (monitor) {
         GdkRectangle work;
         gdk_monitor_get_workarea(monitor, &work);
         int width = 0, height = 0;
         gtk_window_get_size(GTK_WINDOW(main_window), &width, &height);

         if (width > work.width || height > work.height) {
            gtk_window_resize(GTK_WINDOW(main_window), MIN(width, work.width), MIN(height, work.height));
         }
      }
      gtk_ui_zoom_recheck();
   }
   // Fonts are handled entirely by the [gtk-css] section (see cfg.gtkcss.c)
   int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), status_tab);

   if (index != -1) {
      gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
   }

   /* Window placement runs after mapping; reassert once after it finishes. The helper is idempotent and does not toggle the configured flag. */
   if (cfg_fullscreen) {
      g_idle_add(fullscreen_later, NULL);
   }
   g_timeout_add(1000, fullscreen_startup_check, NULL);
   gtk_widget_grab_focus(GTK_WIDGET(chat_entry));

   ui_print(NULL, "%srustyrig client started", get_chat_ts(now));

   return false;
}

gboolean is_widget_or_descendant_focused(GtkWidget *ancestor) {
   if (!ancestor) {
      return FALSE;
   }
   GtkWidget *toplevel = gtk_widget_get_toplevel(ancestor);

   if (!GTK_IS_WINDOW(toplevel)) {
      return FALSE;
   }
   GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

   for (GtkWidget *w = focused ; w ; w = gtk_widget_get_parent(w)) {
      if (w == ancestor) {
         return TRUE;
      }
   }

   return FALSE;
}

bool gui_fullscreen_toggle(void) {
   if (cfg_fullscreen) {
      gtk_window_unfullscreen(GTK_WINDOW(main_window));
      gtk_window_set_decorated(GTK_WINDOW(main_window), TRUE);
   } else {
      gtk_window_fullscreen(GTK_WINDOW(main_window));
      gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
   }
   cfg_fullscreen = !cfg_fullscreen;

   return false;
}

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

   if (!strcasecmp(tab, "admin")) {
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
         char *path = rrclient_user_config_path();
         gboolean saved = path && rrclient_database_config_save(path);
         free(path);

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


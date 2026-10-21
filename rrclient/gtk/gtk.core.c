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
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>
#include <rrclient/media.h>
#include <rrclient/vfo.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.freqentry.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include <rrclient/gtk/gtk.audio-routing.h>
#include <rrclient/gtk/gtk.receiver-directory.h>
#include <rrclient/gtk/gtk.resources.h>
#include <rrclient/gtk/gtk.files.h>
#include <rrclient/gtk/gtk.editcfg.h>
#include <rrclient/gtk/gtk.tabs.h>
#include <rrclient/gtk/gtk.help.h>

#define MSGBUF_SIZE 8096

extern dict *cfg;
extern time_t now;
extern bool dying;              // main.c
GtkWidget *main_window = NULL;
GtkWidget *conn_button = NULL;
GtkWidget *freq_entry = NULL;
GtkWidget *main_notebook = NULL;
GtkWidget *status_tab = NULL;
GtkWidget *log_tab = NULL;
GtkWidget *host_log_tab = NULL;
GtkCssProvider *css_provider = NULL;
bool cfg_use_gtk = true;        // Default to using GTK3
extern GtkWidget *init_log_tab(void);
extern GtkWidget *init_host_log_tab(void);
static int cfg_ui_gtk_main_tabstrip = GTK_POS_BOTTOM; // module-local
extern bool cfg_ui_gtk_vfo_on_top;  // Defined by the rrclient host.
extern bool cfg_fullscreen;

static void frontend_gtk_config_refresh(const char *key) {
   (void)key;
   cfg_fullscreen = cfg_get_bool("ui.full-screen", false);
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

const char *gtk_mirc_color_name(unsigned int n) {
   static const char *colors[] = {
      "bright-white", "black", "blue", "green", "bright-red", "brown",
      "magenta", "orange", "bright-yellow", "bright-green", "cyan",
      "bright-cyan", "bright-blue", "bright-magenta", "bright-black", "white"
   };

   return n < 16 ? colors[n] : n == 99 ? "default" : irc_color_rgb(n);
}

guint8 gtk_formatting_control(gunichar token) {
   switch (token) {
      case 'B': {
         return 0x02;
      }
      case 'C': {
         return 0x03;
      }
      case 'H': {
         return 0x04;
      }
      case 'I': {
         return 0x1d;
      }
      case 'O': {
         return 0x0f;
      }
      case 'R': {
         return 0x16;
      }
      case 'U': {
         return 0x1f;
      }
      case 'S': {
         return 0x1e;
      }
      case 'M': {
         return 0x11;
      }
      default: {
         return 0;
      }
   }
}

const char *gtk_formatting_token(guint8 control) {
   switch (control) {
      case 0x02: {
         return "B";
      }
      case 0x03: {
         return "C";
      }
      case 0x04: {
         return "H";
      }
      case 0x1d: {
         return "I";
      }
      case 0x0f: {
         return "O";
      }
      case 0x16: {
         return "R";
      }
      case 0x1f: {
         return "U";
      }
      case 0x1e: {
         return "S";
      }
      case 0x11: {
         return "M";
      }
      default: {
         return NULL;
      }
   }
}

/* Keep independent IRC styles in a fixed nesting order for valid Pango markup, including when a color changes while bold/italic/reverse are active. */
typedef struct {
   bool bold, italic, underline, strike, mono, reverse;
   char fg[8], bg[8];
} GtkIrcStyle;

static void gtk_irc_style_tags(GString *out, const GtkIrcStyle *style, bool open) {
   const char *fg = style->fg[0] ? style->fg : NULL;
   const char *bg = style->bg[0] ? style->bg : NULL;

   if (style->reverse) {
      const char *swap = fg;
      fg = bg ? bg : "black";
      bg = swap ? swap : "white";
   }

   if (open) {
      if (style->bold) {
         g_string_append(out, "<b>");
      }

      if (style->italic) {
         g_string_append(out, "<i>");
      }

      if (style->underline) {
         g_string_append(out, "<u>");
      }

      if (style->strike) {
         g_string_append(out, "<s>");
      }

      if (style->mono) {
         g_string_append(out, "<tt>");
      }

      if (fg || bg) {
         g_string_append(out, "<span");

         if (fg) {
            g_string_append_printf(out, " foreground=\"%s\"", fg);
         }

         if (bg) {
            g_string_append_printf(out, " background=\"%s\"", bg);
         }
         g_string_append_c(out, '>');
      }
   } else {
      if (fg || bg) {
         g_string_append(out, "</span>");
      }

      if (style->mono) {
         g_string_append(out, "</tt>");
      }

      if (style->strike) {
         g_string_append(out, "</s>");
      }

      if (style->underline) {
         g_string_append(out, "</u>");
      }

      if (style->italic) {
         g_string_append(out, "</i>");
      }

      if (style->bold) {
         g_string_append(out, "</b>");
      }
   }
}

static char *gtk_colorize_text(const char *in) {
   if (!in) {
      return NULL;
   }
   GString *out = g_string_new(NULL);
   GtkIrcStyle style = {
      0
   };
   bool open = false;
   const char *p = in;
   while (*p) {
      unsigned char control = (unsigned char)*p;
      size_t used = 1;
      /* Legacy application style tags share the same style state as IRC. */
      static const struct {
         const char *tag;
         unsigned char control;
         int enabled;
      } tags[] = {
         {
            "{bold}", 0x02, 1
         }, {
            "{bold-off}", 0x02, 0
         },
         {
            "{italic}", 0x1d, 1
         }, {
            "{italic-off}", 0x1d, 0
         },
         {
            "{underline}", 0x1f, 1
         }, {
            "{underline-off}", 0x1f, 0
         },
         {
            "{reset}", 0x0f, 0
         }
      };
      int enabled = -1;

      if (*p == '{') {
         for (size_t i = 0 ; i < G_N_ELEMENTS(tags) ; i++) {
            size_t length = strlen(tags[i].tag);

            if (!strncmp(p, tags[i].tag, length)) {
               control = tags[i].control;
               enabled = tags[i].enabled;
               used = length;
               break;
            }
         }
      }

      if (control == 0x1b) {
         p++;

         if (*p == '[') {
            p++;
            while (*p && !isalpha((unsigned char)*p)) {
               p++;
            }

            if (*p) {
               p++;
            }
         }
         continue;
      }

      if (strchr("\002\003\004\017\021\026\035\036\037", control)) {
         if (open) {
            gtk_irc_style_tags(out, &style, false);
            open = false;
         }

         if (control == 0x03 || control == 0x04) {
            irc_color_t color;
            used = irc_color_parse(p, &color);

            if (color.reset) {
               style.fg[0] = style.bg[0] = '\0';
            } else {
               for (unsigned component = 0 ; component < (color.background ? 2u : 1u) ; component++) {
                  char *dest = component ? style.bg : style.fg;
                  unsigned value = component ? color.bg : color.foreground;

                  if (color.hex) {
                     snprintf(dest, 8, "#%06x", value);
                  } else {
                     const char *rgb = irc_color_rgb(value);
                     snprintf(dest, 8, "%s", rgb ? rgb : "");
                  }
               }
            }
         } else if (control == 0x0f) {
            style = (GtkIrcStyle) {
               0
            };
         } else {
            bool *attribute = control == 0x02 ? &style.bold : control == 0x1d ? &style.italic :
               control == 0x1f ? &style.underline : control == 0x1e ? &style.strike : control == 0x11 ? &style.mono : &style.reverse;
            *attribute = enabled < 0 ? !*attribute : enabled != 0;
         }
         p += used;
         continue;
      }

      if (!open) {
         gtk_irc_style_tags(out, &style, true);
         open = true;
      }
      const char *next = strpbrk(p + 1, "{\033\002\003\004\017\021\026\035\036\037");
      size_t length = next ? (size_t)(next - p) : strlen(p);
      char *escaped = g_markup_escape_text(p, length);
      g_string_append(out, escaped);
      g_free(escaped);
      p += length;
   }

   if (open) {
      gtk_irc_style_tags(out, &style, false);
   }
   char *result = strdup(out->str);
   g_string_free(out, TRUE);

   return result;
}

/* PARITY: rustyrig-www/js/webui.js msg_timestamp; only presentation shrinks. Time formatting and its single separator remain owned by get_chat_ts(). */
char *gtk_colorize_string(const char *in) {
   const char *end = in && !strncmp(in, "\00314[", 4) ? strstr(in, "]\017") : NULL;

   if (!end) {
      return gtk_colorize_text(in);
   }
   end += 2;
   char *stamp = g_strndup(in, end - in);
   char *prefix = gtk_colorize_text(stamp);
   g_free(stamp);
   char *suffix = gtk_colorize_text(end);

   if (!prefix || !suffix) {
      g_free(prefix);
      g_free(suffix);

      return NULL;
   }
   char *result = g_strdup_printf("<span size=\"smaller\">%s</span>%s", prefix, suffix);
   g_free(prefix);
   g_free(suffix);

   return result;
}

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
      Log(LOG_WARN, "ui.gtk3", "ui_print_gtk: gtk_colorize_string failed");
   }

   GtkTextIter end;

   gtk_text_buffer_get_end_iter(target_buffer, &end);
   int start_offset = gtk_text_iter_get_offset(&end);
   gtk_text_buffer_insert_markup(target_buffer, &end, colorized, -1);
   gtk_chat_highlight_prefix(target_view, start_offset);
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
   g_signal_connect(dialog, "key-press-event", G_CALLBACK(on_confirm_dialog_key), NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
   gboolean confirmed = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);

   return confirmed;
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)event;
   (void)data;
   event_emit("client.quit.request", NULL, NULL);

   return TRUE;                 // core handles shutdown after GTK confirmation
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
   gtk_ui_zoom_attach(main_window, gtk_wrapping_tabs_new(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip));
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), FALSE);
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);

   // ADMIN tab (alt-1)
   admin_tab = init_admin_tab();

   // CONFIG tab (alt-2)
   config_tab = init_config_tab();

   // Host LOG tab (alt-3)
   host_log_tab = init_host_log_tab();

   // LOG tab (alt-4)
   log_tab = init_log_tab();

   //// CHAT stuff (alt-5+)...
   chat_init();

   // Shared serial console page, with top-aligned endpoint subtabs.
   extern GtkWidget *init_terminal_tab(void);
   init_terminal_tab();

   // GTK Signals
   g_signal_connect(main_window, "window-state-event", G_CALLBACK(on_window_state), NULL);
   g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   g_signal_connect(main_window, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
   g_signal_connect(main_window, "delete-event", G_CALLBACK(on_window_delete), NULL);

   // bind our hotkeys
   gui_hotkey_register(main_window);

   // Video (webcam) frame listener
   extern void rrclient_webcam_register(void);  // gtk.webcam.c
   rrclient_webcam_register();

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

   // enforce fullscreen if set
   if (cfg_fullscreen) {
      Log(LOG_INFO, "ui.gtk3", "Going fullscreen since cfg:ui.full-screen is set!");

      g_idle_add(fullscreen_later, NULL);
   }
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

static void frontend_gtk_apply_vfo_state(const char *vfo, long freq, const char *mode, int width, int power, bool ptt) {
   GtkWidget *entry = freq_entry;

   if (entry) {
      GtkFreqEntry *fe = GTK_FREQ_ENTRY(entry);
      bool available = vfo_state_get_long(vfo, "cat.state.freq", -1) >= 0;
      gtk_widget_set_tooltip_text(entry, available ? "Observed frequency" : "Frequency unavailable");

      if (!available) {
         gtk_freq_entry_set_unavailable(fe);
         modebox_update_state(mode, width);

         return;
      }
      char selection[256];
      snprintf(selection, sizeof(selection), "%s/%s", rrclient_media_active_room(), vfo);
      const char *displayed = g_object_get_data(G_OBJECT(entry), "rr-displayed-vfo");

      if (!displayed || strcmp(displayed, selection) || !gtk_freq_entry_is_editing(fe)) {
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
   bool ptt, has_mode;
} pending_vfo_paint;
static gboolean frontend_gtk_flush_vfo(gpointer unused) {
   pending_vfo_paint.source = 0;
   const char *server = rrclient_selected_server();
   const char *room = rrclient_media_active_room();

   if (!dying && !strcmp(pending_vfo_paint.server, server ? server : "") &&
      !strcmp(pending_vfo_paint.room, room ? room : "") && pending_vfo_paint.vfo[0] == vfo_state_get_active()) {
      frontend_gtk_apply_vfo_state(pending_vfo_paint.vfo, pending_vfo_paint.freq, pending_vfo_paint.has_mode ? pending_vfo_paint.mode : NULL, pending_vfo_paint.
         width, pending_vfo_paint.power, pending_vfo_paint.ptt);
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
   pending_vfo_paint.freq = freq;
   pending_vfo_paint.width = width;
   pending_vfo_paint.power = power;
   pending_vfo_paint.ptt = ptt;

   if (!pending_vfo_paint.source) {
      pending_vfo_paint.source = g_idle_add(frontend_gtk_flush_vfo, NULL);
   }
}

static void frontend_gtk_freq_set(long freq) {
   GtkWidget *entry = freq_entry;

   if (entry) {
      GtkFreqEntry *fe = GTK_FREQ_ENTRY(entry);

      if (!gtk_freq_entry_is_editing(fe)) {
         gtk_freq_entry_set_frequency(fe, freq);
      }
   }
}

static void frontend_gtk_mode_set(const char *mode) {
   modebox_update_state(mode, 0);
}

static void frontend_gtk_conn_button(int connected) {
   update_connection_button(connected, conn_button);
}

static void frontend_gtk_ptt_set_online(bool online) {
   ptt_button_set_online(online);
}

static void frontend_gtk_ptt_set_state(bool active) {
   ptt_button_set_state(active);
}

static void frontend_gtk_ptt_tot_expired(int tot_secs) {
   ptt_button_tot_expired(tot_secs);
}

static void frontend_gtk_ptt_refresh(void) {
   ptt_button_refresh();
}

static bool frontend_gtk_ptt_hotkey_toggle(void) {
   return ptt_button_hotkey_toggle();
}

static void frontend_gtk_codec_set_active(bool is_tx, const char *codec) {
   codec_picker_set_active(is_tx, codec);
}

static void frontend_gtk_syslog_clear(void) {
   syslog_clear();
}

static void frontend_gtk_focus_tab(const char *tab) {
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

static void frontend_gtk_switch_window(int id) {
   gtk_tabs_select(GTK_NOTEBOOK(main_notebook), id);
}

static bool frontend_gtk_confirm_dialog(const char *message) {
   GtkWindow *parent = NULL;

   if (main_window && GTK_IS_WINDOW(main_window)) {
      parent = GTK_WINDOW(main_window);
   }

   return ui_confirm_dialog(parent, message);
}

static void frontend_gtk_alert(const char *message) {
   alert_dialog(GTK_WINDOW(main_window), MSG_ERROR, message ? message : "");
}

static void frontend_gtk_quit_request(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (ui_confirm_dialog(main_window && GTK_IS_WINDOW(main_window)
         ? GTK_WINDOW(main_window) : NULL, "Confirm quit?")) {
      extern bool dying;
      dying = true;
   }
}

static void frontend_gtk_edit_config(const char *path) {
   gui_edit_config(path);
}

static void frontend_gtk_bell(void) {
   GdkDisplay *display = gtk_widget_get_display(main_window ? main_window : chat_textview);

   if (display) {
      gdk_display_beep(display);
   }
}

static void frontend_gtk_notify(const char *title, const char *message) {
#ifdef USE_LIBNOTIFY
   ui_message_notify(title, message);
#endif
}

static void frontend_gtk_show_server_chooser(void) {
   gtk_show_server_chooser();
}

extern void gtk_webcam_show(bool show);   // gtk.webcam.c
static void frontend_gtk_webcam_show(bool show) {
   gtk_webcam_show(show);
}

static guint frontend_gtk_roster_source;
static gboolean frontend_gtk_flush_roster(gpointer unused) {
   frontend_gtk_roster_source = 0;

   if (!dying) {
      userlist_redraw_gtk();
   }

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_userlist_redraw(void) {
   // One network sweep can deliver many observations. Drain audio/transport
   // first, then paint the latest roster once instead of inside every callback.
   if (!dying && !frontend_gtk_roster_source) {
      frontend_gtk_roster_source = g_idle_add(frontend_gtk_flush_roster, NULL);
   }
}

static void frontend_gtk_userlist_set_visible(bool visible) {
   userlist_set_visible(visible);
}

static void frontend_gtk_room_state_changed(const char *room) {
   userlist_room_state_changed(room);
}

static void frontend_gtk_userlist_room_vfos_changed(const char *room) {
   gtk_chat_room_vfos_changed(room);
}

static void frontend_gtk_chat_clear(void) {
   gtk_text_buffer_set_text(text_buffer, "", -1);
}

static void frontend_gtk_rx_volume(int value) {
   /* Core state can arrive before the GTK VFO panel has been built. */
   if (!rx_vol_slider || !GTK_IS_RANGE(rx_vol_slider)) {
      return;
   }
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), value);
}

static void frontend_gtk_chat_room_add(const char *room) {
   gtk_chat_room_add(room);
}

static void frontend_gtk_chat_room_remove(const char *room) {
   gtk_chat_room_remove(room);
}

static void frontend_gtk_chat_room_topic(const char *room, const char *topic) {
   gtk_chat_room_set_topic(room, topic);
}

static void frontend_gtk_chat_show_status(void) {
   gtk_chat_show_status();
}

static void frontend_gtk_chat_set_authoritative_room(const char *room) {
   gtk_chat_set_authoritative_room(room);
}

static void frontend_gtk_chat_query_add(const char *who) {
   gtk_chat_query_add(who);
}

static void frontend_gtk_chat_room_vfos_changed(const char *room) {
   gtk_chat_room_vfos_changed(room);
}

static const char *frontend_gtk_chat_current_room(void) {
   return gtk_chat_current_room();
}

static void frontend_gtk_vprint(const char *window, const char *fmt, va_list ap) {
   ui_print_gtk(window, fmt, ap);
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

static bool frontend_gtk_init(int *argc, char ***argv) {
   gtk_init(argc, argv);
#ifdef USE_LIBNOTIFY

   if (!ui_notify_init()) {
      Log(LOG_WARN, "gtk.notify", "Desktop notifications unavailable");
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
   gtk_resources_init();
   gtk_files_init();
   // Local client log pane (GTK log tab) mirrors client logs.
   frontend_gtk_log_callback = log_add_callback_token(log_print_va);
   extern bool rr_module_token_add(rr_event_token_t token);
   rr_module_token_add(event_on_token("client.quit.request", frontend_gtk_quit_request, NULL));
   frontend_gtk_update_source = g_timeout_add(1000, frontend_gtk_update_now, NULL);

   return false;
}

static void frontend_gtk_run(void) {
   gtk_main();
}

static void frontend_gtk_quit(void) {
   if (gtk_main_level() > 0) {
      gtk_main_quit();
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

static void frontend_gtk_irc_nick_prompt(const char *profile, const char *nickname) {
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

extern void gtk_supervision_result(const char *, rrconn_t *);
extern void gtk_admin_result(const char *, rrconn_t *);

const rr_frontend_ops_t gtk_frontend_ops = {
   .supervision_result = gtk_supervision_result,
   .admin_result = gtk_admin_result,
   .irc_nick_prompt = frontend_gtk_irc_nick_prompt,
   .name = "gtk",
   .init = frontend_gtk_init,
   .run = frontend_gtk_run,
   .quit = frontend_gtk_quit,
   .vfo_state = frontend_gtk_vfo_state,
   .vfo_widths = frontend_gtk_vfo_widths,
   .freq_set = frontend_gtk_freq_set,
   .mode_set = frontend_gtk_mode_set,
   .connection_state = frontend_gtk_conn_button,
   .conn_button_update = frontend_gtk_conn_button,
   .chat_incoming = gtk_chat_incoming,
   .chat_room_add = frontend_gtk_chat_room_add,
   .chat_room_remove = frontend_gtk_chat_room_remove,
   .chat_room_topic = frontend_gtk_chat_room_topic,
   .chat_show_status = frontend_gtk_chat_show_status,
   .chat_set_authoritative_room = frontend_gtk_chat_set_authoritative_room,
   .chat_room_vfos_changed = frontend_gtk_chat_room_vfos_changed,
   .chat_current_room = frontend_gtk_chat_current_room,
   .chat_status_active = gtk_chat_status_active,
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
   extern void gtk_terminal_shutdown(void);
   gtk_terminal_shutdown();
   gtk_help_shutdown();
   gtk_resources_shutdown();
   gtk_files_shutdown();
   gtk_editcfg_shutdown();
   gtk_receiver_directory_shutdown();
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
   gtk_userlist_stop_timers();
   extern void gtk_winmgr_stop_sources(void);   // gtk.winmgr.c
   gtk_winmgr_stop_sources();
   extern bool dying;

   if (!dying && main_window && GTK_IS_WINDOW(main_window)) {
      gtk_widget_destroy(main_window);
      main_window = NULL;
   }

   if (gtk_main_level() > 0) {
      gtk_main_quit();
   }
}

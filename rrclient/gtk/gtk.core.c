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
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "content=%d toolbar(min=%d nat=%d alloc=%d) tabs(min=%d nat=%d alloc=%d)", available, toolbar_min, toolbar_nat, toolbar_alloc,
         tabs_min, tabs_nat, tabs_alloc);
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
   g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "Effective startup setting ui.full-screen=%s (raw=%s)", cfg_fullscreen ? "true" : "false", cfg_get("ui.full-screen") ? cfg_get(
      "ui.full-screen") : "(absent)");
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
   /* Markup insertion and prefix highlighting may invalidate end. Do not
    * reuse an iterator across mutations of indexable buffer content. */
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
   g_log("RustyRig-GTK", cfg_fullscreen && !actual ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE, "Startup window state: requested_fullscreen=%s actual_fullscreen=%s maximized=%s",
      cfg_fullscreen ? "yes" : "no", actual ? "yes" : "no", (state & GDK_WINDOW_STATE_MAXIMIZED) ? "yes" : "no");

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
static gint64 screensaver_last_activity;
static GPid screensaver_pid;
static gint screensaver_pointer_x, screensaver_pointer_y;
static GdkScreen *screensaver_pointer_screen;

static void screensaver_child_exited(GPid pid, gint status, gpointer data) {
   (void)status;
   (void)data;

   if (screensaver_pid == pid) {
      screensaver_pid = 0;
   }
   g_spawn_close_pid(pid);
   screensaver_last_activity = g_get_monotonic_time();
}

static void screensaver_activity(void) {
   screensaver_last_activity = g_get_monotonic_time();
}
static gboolean screensaver_activity_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
   (void)widget;
   (void)data;

   if (event->type == GDK_KEY_PRESS || event->type == GDK_BUTTON_PRESS || event->type == GDK_SCROLL || event->type == GDK_MOTION_NOTIFY) {
      screensaver_activity();
   }

   return FALSE;
}
static gboolean screensaver_launch(void) {
   if (screensaver_pid) {
      return FALSE;
   }
   gchar program[] = "gmatrix";
   gchar *argv[] = {
      program, NULL
   };
   GError *error = NULL;
   GPid pid = 0;

   if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error)) {
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to launch gmatrix: %s", error->message);
      g_clear_error(&error);
      screensaver_activity();

      return FALSE;
   }
   screensaver_pid = pid;
   g_child_watch_add(pid, screensaver_child_exited, NULL);
   screensaver_activity();

   return TRUE;
}
static gboolean screensaver_idle_check(gpointer unused) {
   (void)unused;

   if (!main_window || !GTK_IS_WINDOW(main_window)) {
      return G_SOURCE_CONTINUE;
   }
   /* Pointer movement, even over nested GTK controls, counts as activity. */
   GdkDisplay *display = gtk_widget_get_display(main_window);
   GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
   GdkDevice *pointer = seat ? gdk_seat_get_pointer(seat) : NULL;

   if (pointer) {
      gint x, y;
      GdkScreen *screen = NULL;
      gdk_device_get_position(pointer, &screen, &x, &y);

      if (x != screensaver_pointer_x || y != screensaver_pointer_y || screen != screensaver_pointer_screen) {
         screensaver_pointer_x = x;
         screensaver_pointer_y = y;
         screensaver_pointer_screen = screen;
         screensaver_activity();
      }
   }
   int timeout = cfg_get_int("ui.screensaver.timeout", 3600);
   const char *selected = cfg_get("ui.screensaver");

   if (!selected || strcmp(selected, "matrix") || timeout <= 0 || screensaver_pid || !gtk_window_is_active(GTK_WINDOW(main_window))) {
      screensaver_activity();

      return G_SOURCE_CONTINUE;
   }
   GdkWindow *window = gtk_widget_get_window(main_window);

   if (!window || !(gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN)) {
      screensaver_activity();

      return G_SOURCE_CONTINUE;
   }

   if (g_get_monotonic_time() - screensaver_last_activity >= (gint64) timeout * G_USEC_PER_SEC) {
      screensaver_launch();
   }

   return G_SOURCE_CONTINUE;
}

/* Spawn the independently packaged screensaver; never run it in the rrclient widget tree. GTK owns the Escape-prefix event handling here. */
static gboolean matrix_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
   (void)widget;
   (void)data;
   static gint64 escape_at;
   gint64 now = g_get_monotonic_time();

   if (event->keyval == GDK_KEY_Escape) {
      escape_at = now;

      return FALSE;
   }
   gboolean armed = escape_at && now - escape_at <= 1500000;
   escape_at = 0;

   if (!armed || (event->keyval != GDK_KEY_grave && event->keyval != GDK_KEY_asciitilde) ||
      (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK))) {
      return FALSE;
   }
   screensaver_launch();

   return TRUE;
}

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


/* Kiwi controls run on the GTK owner thread. Changes are fire-and-forget
 * events to the core; no bridge IPC or network operations run in GTK. */
typedef struct {
   RRFloatPanel *panel;
   GtkWidget *grid, *status, *scales[13];
   char vfo;
   char driver[32];
   gboolean local_room, connected, online, updating;
   guint refresh_timer;
} KiwiControlPanel;
static KiwiControlPanel kiwi_controls;
static const struct {
   const char *key, *label;
   double minimum, maximum, initial, step;
} kiwi_controls_spec[] = {
   {"agc", "AGC enabled", 0, 1, 1, 1},
   {"hang", "AGC hang", 0, 1, 0, 1},
   {"threshold", "AGC threshold (dB)", -130, 0, -100, 1},
   {"slope", "AGC slope", 0, 20, 6, 1},
   {"decay", "AGC decay (ms)", 50, 5000, 1000, 50},
   {"manual-gain", "Manual gain", 0, 100, 50, 1},
   {"squelch", "Squelch enabled", 0, 1, 0, 1},
   {"squelch-threshold", "Squelch threshold", 0, 100, 0, 1},
   {"noise-blanker", "Noise blanker", 0, 1, 0, 1},
   {"blanker-gate", "Blanker gate (us)", 100, 5000, 100, 50},
   {"blanker-threshold", "Blanker threshold (%)", 0, 100, 50, 1},
   {"click-blanker", "Click blanker", 0, 1, 0, 1},
   {"de-emphasis", "De-emphasis", 0, 1, 0, 1}
};
static double kiwi_values[26][G_N_ELEMENTS(kiwi_controls_spec)];
static gboolean kiwi_values_initialized[26];
static void kiwi_panel_request_status(void);

static const char *kiwi_driver_label(const char *driver) {
   if (!driver || !*driver) return "Remote SDR";
   if (!g_ascii_strcasecmp(driver, "kiwi")) return "KiwiSDR";
   if (!g_ascii_strcasecmp(driver, "rtl_tcp")) return "RTL-SDR";
   if (!g_ascii_strcasecmp(driver, "sdrplay")) return "SDRplay";
   static char label[32];
   g_strlcpy(label, driver, sizeof(label));
   for (char *p = label; *p; p++)
      if (*p == '_' || *p == '-') *p = ' ';
   label[0] = g_ascii_toupper(label[0]);
   return label;
}

static void kiwi_panel_update_driver(void) {
   if (!kiwi_controls.panel) return;
   gboolean kiwi = !g_ascii_strcasecmp(kiwi_controls.driver, "kiwi");
   gtk_widget_set_visible(kiwi_controls.grid, kiwi);
   gtk_widget_set_sensitive(kiwi_controls.grid, kiwi && kiwi_controls.online);
   if (!kiwi_controls.connected) {
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), "Remote SDR disconnected");
   } else if (kiwi) {
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), kiwi_controls.online ?
         "Connected (requested control values)" : "Waiting for KiwiSDR controls");
   } else {
      char *status = g_strdup_printf("Additional controls are not available for %s yet.",
                                     kiwi_driver_label(kiwi_controls.driver));
      gtk_label_set_text(GTK_LABEL(kiwi_controls.status), status);
      g_free(status);
   }
   char *title = g_strdup_printf("%s VFO %c", kiwi_driver_label(kiwi_controls.driver),
                                 kiwi_controls.vfo >= 'A' && kiwi_controls.vfo <= 'Z' ?
                                 kiwi_controls.vfo : '?');
   rr_floating_panel_set_title(kiwi_controls.panel, title);
   g_free(title);
}

static void kiwi_panel_active_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message->payload || message->length != sizeof(rrclient_local_bridge_active_t)) return;
   const rrclient_local_bridge_active_t *active = message->payload;
   if (!memchr(active->driver, 0, sizeof(active->driver)) ||
       (active->vfo && (active->vfo < 'A' || active->vfo > 'Z'))) return;
   gboolean changed = kiwi_controls.vfo != active->vfo ||
      kiwi_controls.connected != active->connected ||
      g_ascii_strcasecmp(kiwi_controls.driver, active->driver) != 0;
   g_strlcpy(kiwi_controls.driver, active->driver, sizeof(kiwi_controls.driver));
   kiwi_controls.connected = active->connected;
   kiwi_controls.local_room = TRUE;
   if (active->vfo) kiwi_controls.vfo = active->vfo;
   vfo_controls_set_sdr_driver(active);
   if (!changed) return;
   kiwi_controls.online = FALSE;
   kiwi_panel_update_driver();
   kiwi_panel_request_status();
}

static void kiwi_panel_request_status(void) {
   if (!kiwi_controls.panel) return;
   rr_event_message_t msg = { .name = RRCLIENT_KIWI_STATUS_REQUEST };
   event_emit_message(&msg);
}
static gboolean kiwi_panel_poll(gpointer unused) {
   (void)unused;
   if (!kiwi_controls.panel) { kiwi_controls.refresh_timer = 0; return G_SOURCE_REMOVE; }
   kiwi_panel_request_status();
   return G_SOURCE_CONTINUE;
}
static void kiwi_panel_close(gpointer unused) {
   (void)unused;
   if (kiwi_controls.panel) rr_floating_panel_hide(kiwi_controls.panel);
}
static char kiwi_last_room[128], kiwi_last_vfo[4];
static void kiwi_slider_changed(GtkRange *range, gpointer data) {
   unsigned index = GPOINTER_TO_UINT(data);
   if (!kiwi_controls.panel || kiwi_controls.updating || !kiwi_controls.online ||
       !kiwi_controls.local_room || kiwi_controls.vfo < 'A' || kiwi_controls.vfo > 'Z' ||
       index >= G_N_ELEMENTS(kiwi_controls_spec)) return;
   char vfo = kiwi_controls.vfo;
   unsigned slot = vfo - 'A';
   int value = (int)gtk_range_get_value(range);
   kiwi_values[slot][index] = value;
   rrclient_kiwi_control_request_t req = { .vfo = vfo };
   g_strlcpy(req.control, kiwi_controls_spec[index].key, sizeof(req.control));
   g_snprintf(req.value, sizeof(req.value), "%d", value);
   rr_event_message_t msg = { .name = RRCLIENT_KIWI_CONTROL_EVENT,
      .payload = &req, .length = sizeof(req) };
   event_emit_message(&msg);
}
static void kiwi_panel_switch_vfo(char vfo) {
   if (!kiwi_controls.panel || vfo < 'A' || vfo > 'Z') return;
   unsigned slot = vfo - 'A';
   if (!kiwi_values_initialized[slot]) {
      for (unsigned i = 0; i < G_N_ELEMENTS(kiwi_controls_spec); i++)
         kiwi_values[slot][i] = kiwi_controls_spec[i].initial;
      kiwi_values_initialized[slot] = TRUE;
   }
   kiwi_controls.updating = TRUE;
   for (unsigned i = 0; i < G_N_ELEMENTS(kiwi_controls_spec); i++)
      gtk_range_set_value(GTK_RANGE(kiwi_controls.scales[i]), kiwi_values[slot][i]);
   kiwi_controls.updating = FALSE;
   kiwi_controls.vfo = vfo;
   kiwi_panel_update_driver();
}
static void kiwi_panel_status_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!kiwi_controls.panel || !message->payload ||
       message->length != sizeof(rrclient_kiwi_control_status_t)) return;
   const rrclient_kiwi_control_status_t *status = message->payload;
   if (status->vfo != kiwi_controls.vfo) return;
   kiwi_controls.online = kiwi_controls.local_room &&
      !g_ascii_strcasecmp(kiwi_controls.driver, "kiwi") && status->available;
   kiwi_panel_update_driver();
}
static void kiwi_panel_vfo_snapshot(const rrclient_vfo_snapshot_t *snapshot) {
   g_strlcpy(kiwi_last_room, snapshot->room, sizeof(kiwi_last_room));
   g_strlcpy(kiwi_last_vfo, snapshot->vfo, sizeof(kiwi_last_vfo));
   if (!kiwi_controls.panel) return;
   kiwi_controls.local_room = !strcmp(snapshot->room, "&local-sdr");
   kiwi_controls.online = FALSE;
   gtk_widget_set_sensitive(kiwi_controls.grid, FALSE);
   if (kiwi_controls.local_room && snapshot->vfo[0] >= 'A' && snapshot->vfo[0] <= 'Z' &&
       !snapshot->vfo[1]) kiwi_panel_switch_vfo(snapshot->vfo[0]);
   else gtk_label_set_text(GTK_LABEL(kiwi_controls.status), "Select a local KiwiSDR VFO");
   kiwi_panel_request_status();
}
static void kiwi_panel_show(void) {
   if (kiwi_controls.panel) { rr_floating_panel_show(kiwi_controls.panel); return; }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;
   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) return;
   kiwi_controls.panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook),
                                               "remote-sdr-controls", "Remote SDR");
   if (!kiwi_controls.panel) return;
   rr_floating_panel_set_close_callback(kiwi_controls.panel, kiwi_panel_close, NULL);
   GtkWidget *body = rr_floating_panel_body(kiwi_controls.panel);
   kiwi_controls.status = gtk_label_new("Remote SDR disconnected");
   gtk_box_pack_start(GTK_BOX(body), kiwi_controls.status, FALSE, FALSE, 3);
   kiwi_controls.grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(kiwi_controls.grid), 4);
   gtk_grid_set_column_spacing(GTK_GRID(kiwi_controls.grid), 8);
   for (unsigned i = 0; i < G_N_ELEMENTS(kiwi_controls_spec); i++) {
      GtkWidget *label = gtk_label_new(kiwi_controls_spec[i].label);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(kiwi_controls.grid), label, 0, i, 1, 1);
      GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
         kiwi_controls_spec[i].minimum, kiwi_controls_spec[i].maximum,
         kiwi_controls_spec[i].step);
      kiwi_controls.scales[i] = scale;
      gtk_widget_set_size_request(scale, 200, -1);
      gtk_scale_set_digits(GTK_SCALE(scale), 0);
      gtk_grid_attach(GTK_GRID(kiwi_controls.grid), scale, 1, i, 1, 1);
      g_signal_connect(scale, "value-changed", G_CALLBACK(kiwi_slider_changed),
                       GUINT_TO_POINTER(i));
   }
   gtk_widget_set_sensitive(kiwi_controls.grid, FALSE);
   gtk_box_pack_start(GTK_BOX(body), kiwi_controls.grid, FALSE, FALSE, 2);
   gtk_widget_show_all(rr_floating_panel_widget(kiwi_controls.panel));
   kiwi_panel_update_driver();
   kiwi_controls.refresh_timer = g_timeout_add_seconds(2, kiwi_panel_poll, NULL);
   rrclient_vfo_snapshot_t initial = {0};
   g_strlcpy(initial.room, kiwi_last_room, sizeof(initial.room));
   g_strlcpy(initial.vfo, kiwi_last_vfo, sizeof(initial.vfo));
   kiwi_panel_vfo_snapshot(&initial);
}
void gtk_local_sdr_controls_show(void) {
   kiwi_panel_show();
}

static void gtk_manage_certificates(GtkWidget *unused, gpointer unused_data);
static void frontend_gtk_main_window_destroy(GtkWidget *widget, gpointer unused) {
   (void)unused;
   if (main_window == widget) main_window = NULL;
   if (gtk_main_level() > 0) gtk_main_quit();
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
   /* Preferred-width queries inside size-allocate can force nested GTK
    * layout recalculation on every resize/focus transition. Keep this
    * diagnostic opt-in; never run it on the normal GUI hot path. */
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
   rr_floating_panel_set_reserved_widgets(GTK_OVERLAY(rr_client_overlay), rr_main_toolbar,
      upper_extra, lower, RR_FLOATING_PANEL_SCROLLBAR_INSET);
   g_object_unref(viewport);
   g_signal_connect(main_window, "key-press-event", G_CALLBACK(matrix_key_press), NULL);
   g_signal_connect(main_window, "event", G_CALLBACK(screensaver_activity_event), NULL);
   screensaver_activity();
   g_timeout_add_seconds(1, screensaver_idle_check, NULL);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_notebook), FALSE);
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(main_notebook), cfg_ui_gtk_main_tabstrip);

   // ADMIN tab (alt-1)
   admin_tab = init_admin_tab();

   // CONFIG tab (alt-2)
   config_tab = init_config_tab();
   /* Settings, not a global connection control. */
   GtkWidget *certificates = gtk_button_new_with_label("Manage Certificates");
   GtkWidget *cert_icon = gtk_image_new_from_icon_name("dialog-password-symbolic", GTK_ICON_SIZE_BUTTON);
   gtk_button_set_image(GTK_BUTTON(certificates), cert_icon);
   gtk_button_set_always_show_image(GTK_BUTTON(certificates), TRUE);
   gtk_widget_set_tooltip_text(certificates, "Manage local Ed25519 client keys");
   g_signal_connect(certificates, "clicked", G_CALLBACK(gtk_manage_certificates), NULL);

   if (GTK_IS_BOX(config_tab)) {
      gtk_box_pack_start(GTK_BOX(config_tab), certificates, FALSE, FALSE, 8);
      gtk_widget_show_all(certificates);
   } else {
      /* A settings tab may be a scrolled container: attach to its outer vertical box where possible rather than putting it in the toolbar. */
      GtkWidget *child = GTK_IS_BIN(config_tab) ? gtk_bin_get_child(GTK_BIN(config_tab)) : NULL;

      if (GTK_IS_BOX(child)) {
         gtk_box_pack_start(GTK_BOX(child), certificates, FALSE, FALSE, 8);
         gtk_widget_show_all(certificates);
      } else {
         Log(LOG_WARN, "client.identity", "Config tab has no usable box for certificate action");
         gtk_widget_destroy(certificates);
      }
   }

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
   /* The latest core snapshot is authoritative; no live core lookup in
    * the GTK paint callback. */
   if (!dying) {
      frontend_gtk_apply_vfo_state(pending_vfo_paint.vfo, pending_vfo_paint.room,
         pending_vfo_paint.available, pending_vfo_paint.freq,
         pending_vfo_paint.has_mode ? pending_vfo_paint.mode : NULL,
         pending_vfo_paint.width, pending_vfo_paint.power, pending_vfo_paint.ptt);
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

/* All of these frontend operations may originate in the core callback path.
 * Route them through frontend_gtk_ui_post() below, never synchronously paint. */
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
   if (!dying) ptt_button_hotkey_toggle();
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
   if (G_UNLIKELY(g_thread_self() != frontend_gtk_owner))
      g_log("RustyRig-GTK", G_LOG_LEVEL_CRITICAL, "GTK operation executed on wrong thread: actual=%p expected=%p",
          (void *)g_thread_self(), (void *)frontend_gtk_owner);
#endif
}

static bool frontend_gtk_ptt_hotkey_toggle(void) {
   if (g_thread_self() == frontend_gtk_owner)
      return ptt_button_hotkey_toggle();
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
   /* A synchronous confirmation cannot be marshaled without blocking core.
    * Deny rather than create a modal GTK window on the wrong thread. */
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
   if (!ui_confirm_dialog(parent, "Confirm quit?")) return;

   if (rrclient_config_is_dirty()) {
      GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
         GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
         "Configuration has unsaved changes. Save before quitting?");
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT);
      gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_ACCEPT);
      gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
      gint response = gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
      if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) return;
      if (response == GTK_RESPONSE_ACCEPT) {
         char *path = rrclient_user_config_path();
         gboolean saved = path && rrclient_database_config_save(path);
         free(path);
         if (!saved) {
            GtkWidget *error = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL,
               GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Unable to save the client configuration. Quit cancelled.");
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
   if (!dying) event_emit_message(&(rr_event_message_t){
      .name = "client.ui.roster.redraw", .sender = "core",
      .payload = NULL, .length = 0
   });
}
static void frontend_gtk_roster_redraw_event(const rr_event_message_t *message, void *unused) {
   (void)message;
   (void)unused;
   frontend_gtk_assert_owner();
   if (dying || frontend_gtk_roster_queued) return;
   frontend_gtk_roster_queued = true;
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_flush_roster, NULL, NULL);
   frontend_gtk_roster_source = g_source_attach(source, frontend_gtk_event_context);
   g_source_unref(source);
}

/* Legacy frontend callbacks can originate on the core transport thread.
 * Queue owned arguments before touching GTK widgets. */

typedef enum { GTK_UI_VISIBLE, GTK_UI_ROOM_STATE, GTK_UI_ROOM_VFOS,
               GTK_UI_CHAT_CLEAR, GTK_UI_RX_VOLUME, GTK_UI_FREQ,
               GTK_UI_MODE, GTK_UI_CONNECTION, GTK_UI_PTT_ONLINE,
               GTK_UI_PTT_STATE, GTK_UI_PTT_TOT, GTK_UI_PTT_REFRESH } GtkUiAction;
typedef struct { GtkUiAction action; char *room; int value; long freq; } GtkUiDelivery;
static gboolean frontend_gtk_ui_deliver(gpointer opaque) {
   GtkUiDelivery *d = opaque;
   if (dying) return G_SOURCE_REMOVE;
   switch (d->action) {
      case GTK_UI_VISIBLE: userlist_set_visible(d->value != 0); break;
      case GTK_UI_ROOM_STATE: userlist_room_state_changed(d->room); break;
      case GTK_UI_ROOM_VFOS: gtk_chat_room_vfos_changed(d->room); break;
      case GTK_UI_CHAT_CLEAR:
         if (text_buffer) gtk_text_buffer_set_text(text_buffer, "", -1);
         break;
      case GTK_UI_RX_VOLUME:
         if (rx_vol_slider && GTK_IS_RANGE(rx_vol_slider))
            gtk_range_set_value(GTK_RANGE(rx_vol_slider), d->value);
         break;
      case GTK_UI_FREQ:
         if (freq_entry && !gtk_freq_entry_is_editing(GTK_FREQ_ENTRY(freq_entry)))
            gtk_freq_entry_set_frequency(GTK_FREQ_ENTRY(freq_entry), d->freq);
         break;
      case GTK_UI_MODE: modebox_update_state(d->room, 0); break;
      case GTK_UI_CONNECTION: rr_connect_button_update_all(d->value); break;
      case GTK_UI_PTT_ONLINE: ptt_button_set_online(d->value != 0); break;
      case GTK_UI_PTT_STATE: ptt_button_set_state(d->value != 0); break;
      case GTK_UI_PTT_TOT: ptt_button_tot_expired(d->value); break;
      case GTK_UI_PTT_REFRESH: ptt_button_refresh(); break;
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
   } else frontend_gtk_ui_delivery_free(d);
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

/* One-way frontend operations: own strings and execute on GTK's context.
 * The bool-returning frontend operations deliberately remain separate:
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
   if (dying || !message->payload || message->length < sizeof(GtkOwnerMessage)) return;
   GtkOwnerMessage m;
   memcpy(&m, message->payload, sizeof(m));
   if (m.action > GTK_OP_WEBCAM || !m.a_len || !m.b_len) return;
   size_t bytes = message->length - sizeof(m);
   if ((size_t)m.a_len > bytes || (size_t)m.b_len != bytes - m.a_len) return;
   const char *a = (const char *)message->payload + sizeof(m);
   const char *b = a + m.a_len;
   if (a[m.a_len - 1] || b[m.b_len - 1]) return;
   switch ((GtkOwnerAction)m.action) {
      case GTK_OP_CODEC: frontend_gtk_codec_set_active_owner(m.value != 0, a); break;
      case GTK_OP_CLEAR_LOG: frontend_gtk_syslog_clear_owner(); break;
      case GTK_OP_FOCUS: frontend_gtk_focus_tab_owner(a); break;
      case GTK_OP_WINDOW: frontend_gtk_switch_window_owner(m.value); break;
      case GTK_OP_ALERT: frontend_gtk_alert_owner(a); break;
      case GTK_OP_EDIT: frontend_gtk_edit_config_owner(*a ? a : NULL); break;
      case GTK_OP_BELL: frontend_gtk_bell_owner(); break;
      case GTK_OP_NOTIFY: frontend_gtk_notify_owner(a, b); break;
      case GTK_OP_CHOOSER: frontend_gtk_show_server_chooser_owner(); break;
      case GTK_OP_WEBCAM: frontend_gtk_webcam_show_owner(m.value != 0); break;
   }
}
static void frontend_gtk_owner_post(GtkOwnerAction action, const char *a, const char *b, int value) {
   size_t al = strlen(a ? a : "") + 1, bl = strlen(b ? b : "") + 1;
   if (al > 65536 || bl > 65536) return;
   GtkOwnerMessage header = { (uint32_t)action, (uint32_t)al, (uint32_t)bl, value };
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
static void frontend_gtk_syslog_clear(void) { frontend_gtk_owner_post(GTK_OP_CLEAR_LOG, NULL, NULL, 0); }
static void frontend_gtk_focus_tab(const char *tab) { frontend_gtk_owner_post(GTK_OP_FOCUS, tab, NULL, 0); }
static void frontend_gtk_switch_window(int id) { frontend_gtk_owner_post(GTK_OP_WINDOW, NULL, NULL, id); }
static void frontend_gtk_alert(const char *message) { frontend_gtk_owner_post(GTK_OP_ALERT, message, NULL, 0); }
static void frontend_gtk_edit_config(const char *path) { frontend_gtk_owner_post(GTK_OP_EDIT, path, NULL, 0); }
static void frontend_gtk_bell(void) { frontend_gtk_owner_post(GTK_OP_BELL, NULL, NULL, 0); }
static void frontend_gtk_notify(const char *title, const char *message) {
   frontend_gtk_owner_post(GTK_OP_NOTIFY, title, message, 0);
}
static void frontend_gtk_show_server_chooser(void) { frontend_gtk_owner_post(GTK_OP_CHOOSER, NULL, NULL, 0); }
static void frontend_gtk_webcam_show(bool show) { frontend_gtk_owner_post(GTK_OP_WEBCAM, NULL, NULL, show); }

/* Chat widget mutations are always delivered to GTK's owning thread.
 * Never pass a borrowed core dictionary/string across the boundary. */
typedef enum {
   GTK_CHAT_ADD, GTK_CHAT_REMOVE, GTK_CHAT_TOPIC, GTK_CHAT_STATUS,
   GTK_CHAT_AUTH, GTK_CHAT_QUERY, GTK_CHAT_VFOS
} GtkChatAction;
typedef struct {
   GtkChatAction action;
   char *a, *b;
} GtkChatCommand;
static gboolean frontend_gtk_chat_command_run(gpointer opaque) {
   GtkChatCommand *cmd = opaque;
   switch (cmd->action) {
   case GTK_CHAT_ADD: gtk_chat_room_add(cmd->a); break;
   case GTK_CHAT_REMOVE: gtk_chat_room_remove(cmd->a); break;
   case GTK_CHAT_TOPIC: gtk_chat_room_set_topic(cmd->a, cmd->b); break;
   case GTK_CHAT_STATUS: gtk_chat_show_status(); break;
   case GTK_CHAT_AUTH: gtk_chat_set_authoritative_room(cmd->a); break;
   case GTK_CHAT_QUERY: gtk_chat_query_add(cmd->a); break;
   case GTK_CHAT_VFOS: gtk_chat_room_vfos_changed(cmd->a); break;
   }
   return G_SOURCE_REMOVE;
}
static void frontend_gtk_chat_command_free(gpointer opaque) {
   GtkChatCommand *cmd = opaque;
   g_free(cmd->a);
   g_free(cmd->b);
   g_free(cmd);
}
static void frontend_gtk_chat_command(GtkChatAction action, const char *a, const char *b) {
   GtkChatCommand *cmd = g_new0(GtkChatCommand, 1);
   cmd->action = action;
   cmd->a = g_strdup(a);
   cmd->b = g_strdup(b);
   if (!frontend_gtk_event_context || g_thread_self() == frontend_gtk_owner) {
      frontend_gtk_chat_command_run(cmd);
      frontend_gtk_chat_command_free(cmd);
      return;
   }
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_chat_command_run, cmd,
                         frontend_gtk_chat_command_free);
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
   /* Never return a borrowed GtkWidget-backed string to core. Core callers
    * must use their authoritative selected-room state instead. */
   if (g_thread_self() != frontend_gtk_owner) return NULL;
   return gtk_chat_current_room();
}

static void ui_print_gtk_literal(const char *window, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   ui_print_gtk(window, fmt, args);
   va_end(args);
}
/* A core/network callback must never edit a GTK text buffer directly.
 * Snapshot printf output before handing it to the GUI owner thread. */
typedef struct {
   char *window;
   char *message;
} GtkQueuedPrint;

static gboolean frontend_gtk_queued_print(gpointer opaque) {
   GtkQueuedPrint *print = opaque;
   if (!dying) {
      ui_print_gtk_literal(print->window, "%s", print->message);
   }
   return G_SOURCE_REMOVE;
}

static void frontend_gtk_queued_print_free(gpointer opaque) {
   GtkQueuedPrint *print = opaque;
   g_free(print->window);
   g_free(print->message);
   g_free(print);
}

static void frontend_gtk_vprint(const char *window, const char *fmt, va_list ap) {
   if (!fmt || dying) return;
   va_list copy;
   va_copy(copy, ap);
   char *rendered = g_strdup_vprintf(fmt, copy);
   va_end(copy);
   if (g_thread_self() == frontend_gtk_owner) {
      ui_print_gtk_literal(window, "%s", rendered);
      g_free(rendered);
      return;
   }
   GtkQueuedPrint *print = g_new0(GtkQueuedPrint, 1);
   print->window = g_strdup(window);
   print->message = rendered;
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_queued_print, print,
                         frontend_gtk_queued_print_free);
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

/* First-run client identity. Never replace a key silently: doing so would orphan all server-side public-key registrations for this device. */
static char *gtk_identity_path(void) {
   return g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", "client-ed25519.pem", NULL);
}
static bool gtk_identity_save_new(rr_crypto_key_t *key, const char *path) {
   char *dir = g_path_get_dirname(path);
   bool ok = g_mkdir_with_parents(dir, 0700) == 0;
   struct stat st;

   if (ok) {
      ok = lstat(dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
   }

   if (ok) {
      int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

      if (fd >= 0) {
         FILE *f = fdopen(fd, "wb");

         if (f) {
            ok = rr_crypto_key_write_pem_stream(key, f) && fflush(f) == 0 && fsync(fileno(f)) == 0;

            if (fclose(f)) {
               ok = false;
            }
         } else {
            close(fd);
            ok = false;
         }

         if (!ok) {
            unlink(path);
         }
      } else {
         ok = false;
      }
   }
   g_free(dir);

   return ok;
}

/* The GTK key manager deliberately operates only on the local identity. A future network profile can select an identity by stable fingerprint. */
static void identity_message(GtkWindow *parent, GtkMessageType type, const char *message) {
   GtkWidget *dlg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE, "%s", message);
   gtk_dialog_run(GTK_DIALOG(dlg));
   gtk_widget_destroy(dlg);
}
static char *identity_ask_password(GtkWindow *parent, const char *title, bool confirm) {
   GtkWidget *dlg = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   GtkWidget *one = gtk_entry_new(), *two = confirm ? gtk_entry_new() : NULL;
   gtk_entry_set_visibility(GTK_ENTRY(one), FALSE);
   gtk_entry_set_placeholder_text(GTK_ENTRY(one), "Password");
   gtk_box_pack_start(GTK_BOX(box), one, FALSE, FALSE, 5);

   if (two) {
      gtk_entry_set_visibility(GTK_ENTRY(two), FALSE);
      gtk_entry_set_placeholder_text(GTK_ENTRY(two), "Confirm password");
      gtk_box_pack_start(GTK_BOX(box), two, FALSE, FALSE, 5);
   }
   gtk_widget_show_all(dlg);
   char *value = NULL;

   if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
      const char *first = gtk_entry_get_text(GTK_ENTRY(one));

      if (*first && (!two || !strcmp(first, gtk_entry_get_text(GTK_ENTRY(two))))) {
         value = g_strdup(first);
      }
   }
   gtk_widget_destroy(dlg);

   return value;
}
static rr_crypto_key_t *identity_read_key(const char *path, GtkWindow *parent) {
   FILE *f = fopen(path, "rb");

   if (!f) {
      return NULL;
   }
   /* Import accepts both protected and unprotected PEM; never allow OpenSSL to prompt from the terminal. */
   rr_crypto_key_t *key = rr_crypto_key_read_encrypted_pem(f, "");

   if (!key) {
      rewind(f);
      char *pass = identity_ask_password(parent, "Unlock Private Key", false);

      if (pass) {
         key = rr_crypto_key_read_encrypted_pem(f, pass);
         g_free(pass);
      }
   }
   fclose(f);

   if (key) {
      unsigned char proof[64];
      static const char check[] = "rustyrig.client.identity.private-key-check.v1";

      if (!rr_crypto_ed25519_sign(key, check, sizeof(check) - 1, proof)) {
         rr_crypto_key_free(key);
         key = NULL;
      }
   }

   return key;
}
static void identity_menu_action(GtkMenuItem *item, gpointer dialog) {
   gtk_dialog_response(GTK_DIALOG(dialog), GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "response")));
}
static gboolean identity_list_menu(GtkWidget *list, GdkEventButton *event, gpointer dialog) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
      return FALSE;
   }
   GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(list), (gint) event->y);

   if (!row) {
      return FALSE;
   }
   gtk_list_box_select_row(GTK_LIST_BOX(list), row);
   const char *path = g_object_get_data(G_OBJECT(row), "identity-path");
   GtkWidget *menu = gtk_menu_new();
   const char *labels[] = {
      rrclient_identity_get(path) ? "🔒 Lock" : "🔓 Unlock",
      "✏ Rename", NULL, "⬆ Export", NULL, "🗑 Delete"
   };
   const int responses[] = {
      4, 7, 0, 3, 0, 5
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(labels) ; i++) {
      if (!labels[i]) {
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
         continue;
      }
      GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
      g_object_set_data(G_OBJECT(item), "response", GINT_TO_POINTER(responses[i]));
      g_signal_connect(item, "activate", G_CALLBACK(identity_menu_action), dialog);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
   }

   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);

   return TRUE;
}
static char *identity_prompt_name(GtkWindow *parent, const char *title, const char *existing) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons(title, parent, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Letters, digits, _ or -");

   if (existing) {
      gtk_entry_set_text(GTK_ENTRY(entry), existing);
   }
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
   gtk_widget_show_all(dialog);
   char *name = NULL;
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *candidate = gtk_entry_get_text(GTK_ENTRY(entry));

      if (*candidate && strlen(candidate) <= 64 && strspn(candidate, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(candidate)) {
         name = g_strdup(candidate);
         break;
      }
      identity_message(parent, GTK_MESSAGE_WARNING, "Name must contain 1–64 letters, digits, _ or -.");
   }
   gtk_widget_destroy(dialog);

   return name;
}
static void gtk_manage_certificates(GtkWidget *unused, gpointer unused_data) {
   (void)unused;
   (void)unused_data;
   GtkWindow *parent = main_window ? GTK_WINDOW(main_window) : NULL;
   GtkWidget *dlg = gtk_dialog_new_with_buttons("Manage Certificates", parent, GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dlg), 510, -1);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   gtk_container_set_border_width(GTK_CONTAINER(area), 14);
   GtkWidget *description = gtk_label_new("Local Ed25519 identities");
   gtk_label_set_xalign(GTK_LABEL(description), 0);
   gtk_box_pack_start(GTK_BOX(area), description, FALSE, FALSE, 4);
   GtkWidget *list = gtk_list_box_new();
   gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
   gtk_widget_set_size_request(list, 380, 90);
   gtk_box_pack_start(GTK_BOX(area), list, TRUE, TRUE, 6);
   g_signal_connect(list, "button-press-event", G_CALLBACK(identity_list_menu), dlg);
   GtkWidget *status = gtk_label_new("");
   gtk_label_set_xalign(GTK_LABEL(status), 0);
   gtk_box_pack_start(GTK_BOX(area), status, FALSE, FALSE, 8);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "➕ Create New", 1);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "⬇ Import", 2);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "⬆ Export", 3);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "🔒 Lock / Unlock", 4);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "🗑 Delete", 5);
   gtk_dialog_add_button(GTK_DIALOG(dlg), "Lock All", 6);
   char *path = NULL;
   char *selected_path = NULL;
   char *dir = g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", NULL);

   for ( ;; ) {
      GList *rows = gtk_container_get_children(GTK_CONTAINER(list));

      for (GList *item = rows ; item ; item = item->next) {
         gtk_widget_destroy(GTK_WIDGET(item->data));
      }

      g_list_free(rows);
      GDir *directory = g_dir_open(dir, 0, NULL);
      const char *filename;
      GtkListBoxRow *preferred = NULL;
      unsigned count = 0;

      if (directory) {
         while ((filename = g_dir_read_name(directory))) {
            if (!g_str_has_suffix(filename, ".pem")) {
               continue;
            }
            char *candidate = g_build_filename(dir, filename, NULL);
            struct stat st;

            if (lstat(candidate, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
               g_free(candidate);
               continue;
            }
            GtkWidget *row = gtk_list_box_row_new();
            char *display = !strcmp(filename, "client-ed25519.pem") ? g_strdup("Default Client Identity") : g_strndup(filename, strlen(filename) - 4);
            GtkWidget *label = gtk_label_new(display);
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_widget_set_margin_start(label, 12);
            gtk_widget_set_margin_top(label, 8);
            gtk_widget_set_margin_bottom(label, 8);
            gtk_container_add(GTK_CONTAINER(row), label);
            g_object_set_data_full(G_OBJECT(row), "identity-path", candidate, g_free);
            gtk_container_add(GTK_CONTAINER(list), row);

            if (selected_path && !strcmp(selected_path, candidate)) {
               preferred = GTK_LIST_BOX_ROW(row);
            }

            if (!selected_path && count == 0) {
               preferred = GTK_LIST_BOX_ROW(row);
            }
            count++;
            g_free(display);
         }
         g_dir_close(directory);
      }

      if (preferred) {
         gtk_list_box_select_row(GTK_LIST_BOX(list), preferred);
      }
      GtkListBoxRow *highlight = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
      const char *highlight_path = highlight ? g_object_get_data(G_OBJECT(highlight), "identity-path") : NULL;
      gtk_label_set_text(GTK_LABEL(status), !count ?
         "No identities yet. Create or import an Ed25519 key." : rrclient_identity_get(highlight_path) ? "Selected key: Unlocked" : "Selected key: Locked");

      gtk_widget_show_all(dlg);
      int response = gtk_dialog_run(GTK_DIALOG(dlg));
      GtkListBoxRow *selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
      g_free(selected_path);
      selected_path = selected ? g_strdup(g_object_get_data(G_OBJECT(selected), "identity-path")) : NULL;
      g_free(path);
      path = selected_path ? g_strdup(selected_path) : NULL;
      bool safe = path != NULL;

      if (response == GTK_RESPONSE_CLOSE || response == GTK_RESPONSE_DELETE_EVENT) {
         break;
      }

      /* Only the selected row is a target for key operations. */
      if ((response == 3 || response == 4 || response == 5 || response == 7) && !gtk_list_box_get_selected_row(GTK_LIST_BOX(list))) {
         continue;
      }

      if (response == 6) {
         rrclient_identity_lock_all();
         continue;
      }

      if (response == 4) {
         if (rrclient_identity_get(path)) {
            rrclient_identity_lock(path);
         } else if (safe) {
            /* Try an unencrypted key first. An encrypted key prompts once. */
            if (!rrclient_identity_unlock(path, "")) {
               char *pass = identity_ask_password(GTK_WINDOW(dlg), "Unlock Private Key", false);

               if (pass) {
                  if (!rrclient_identity_unlock(path, pass)) {
                     identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to unlock this private key.");
                  }
                  g_free(pass);
               }
            }
         }
         continue;
      }

      if (response == 7 && safe) {
         char *old = g_path_get_basename(path);

         if (g_str_has_suffix(old, ".pem")) {
            old[strlen(old) - 4] = 0;
         }
         char *name = identity_prompt_name(GTK_WINDOW(dlg), "Rename Certificate", old);
         g_free(old);

         if (name) {
            char *base = g_strconcat(name, ".pem", NULL);
            char *target = g_build_filename(dir, base, NULL);

            if (strcmp(path, target) && g_file_test(target, G_FILE_TEST_EXISTS)) {
               identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "That key name already exists.");
            } else if (strcmp(path, target) && rename(path, target)) {
               identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to rename the key file.");
            } else {
               rrclient_identity_lock(path);
               g_free(selected_path);
               selected_path = g_strdup(target);
            }
            g_free(target);
            g_free(base);
            g_free(name);
         }
         continue;
      }

      if (response == 5 && safe) {
         GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(dlg), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "Delete this private key? Servers registered to it will no longer recognize this device. Export a backup first.");
         bool yes = gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES;
         gtk_widget_destroy(confirm);

         if (yes && unlink(path) == 0) {
            rrclient_identity_lock(path);
            g_clear_pointer(&selected_path, g_free);
         }
         continue;
      }

      if (response == 1 || response == 2) {
         rr_crypto_key_t *key = NULL;

         if (response == 1) {
            key = rr_crypto_ed25519_generate();
         } else {
            GtkWidget *chooser =
               gtk_file_chooser_dialog_new("Import Private Key", GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Import",
               GTK_RESPONSE_ACCEPT, NULL);

            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
               char *source = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
               key = identity_read_key(source, GTK_WINDOW(dlg));
               g_free(source);
            }
            gtk_widget_destroy(chooser);
         }
         char *uuid = response == 1 ? identity_prompt_name(GTK_WINDOW(dlg), "Name New Certificate", NULL) : g_uuid_string_random();

         if (!uuid) {
            rr_crypto_key_free(key);
            continue;
         }
         char *basename = g_strdup_printf("%s.pem", uuid);
         char *new_path = g_build_filename(dir, basename, NULL);
         bool stored = key && !g_file_test(new_path, G_FILE_TEST_EXISTS) && gtk_identity_save_new(key, new_path);

         if (stored) {
            g_free(selected_path);
            selected_path = g_strdup(new_path);
         }
         g_free(new_path);
         g_free(basename);
         g_free(uuid);

         if (!stored) {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_ERROR, "Unable to create or import the private key.");
         }
         rr_crypto_key_free(key);
         continue;
      }

      if (response == 3 && safe) {
         rr_crypto_key_t *key = rrclient_identity_get(path);
         bool owned = false;

         if (!key) {
            key = identity_read_key(path, GTK_WINDOW(dlg));
            owned = true;
         }

         if (!key) {
            continue;
         }
         char *password = identity_ask_password(GTK_WINDOW(dlg), "Export Password (required)", true);

         if (password) {
            GtkWidget *chooser = gtk_file_chooser_dialog_new("Export Encrypted Private Key", GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
               GTK_RESPONSE_CANCEL, "_Export", GTK_RESPONSE_ACCEPT, NULL);
            gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);

            if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
               char *destination = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
               /* Create with 0600, not fopen's process-umask-dependent mode. */
               int fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
               bool ok = false;

               if (fd >= 0) {
                  FILE *f = fdopen(fd, "wb");

                  if (f) {
                     ok = rr_crypto_key_write_encrypted_pem_stream(key, f, password) && fflush(f) == 0 && fsync(fileno(f)) == 0;

                     if (fclose(f)) {
                        ok = false;
                     }
                  } else {
                     close(fd);
                  }

                  if (!ok) {
                     unlink(destination);
                  }
               }
               identity_message(GTK_WINDOW(dlg), ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, ok ? "Encrypted private key exported." :
                  "Unable to export private key.");
               g_free(destination);
            }
            gtk_widget_destroy(chooser);
            g_free(password);
         } else {
            identity_message(GTK_WINDOW(dlg), GTK_MESSAGE_WARNING, "Export requires a nonempty matching password.");
         }

         if (owned) {
            rr_crypto_key_free(key);
         }
      }
   }

   gtk_widget_destroy(dlg);
   g_free(path);
   g_free(selected_path);
   g_free(dir);
}
static void gtk_identity_first_run(void) {
   /* Additional identities are first-class; do not show first-run setup solely because the legacy default key is absent. */
   char *keydir = g_build_filename(g_get_user_config_dir(), "rustyrig", "identity", NULL);
   GDir *keys = g_dir_open(keydir, 0, NULL);
   const char *entry;
   bool any = false;

   if (keys) {
      while ((entry = g_dir_read_name(keys))) {
         if (!g_str_has_suffix(entry, ".pem")) {
            continue;
         }
         char *candidate = g_build_filename(keydir, entry, NULL);
         struct stat st;
         any = lstat(candidate, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
         g_free(candidate);

         if (any) {
            break;
         }
      }
      g_dir_close(keys);
   }
   g_free(keydir);

   if (any) {
      return;
   }
   char *path = gtk_identity_path();

   if (g_file_test(path, G_FILE_TEST_EXISTS)) {
      struct stat st;

      if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
         Log(LOG_WARN, "client.identity", "Client key has unsafe ownership or permissions: %s", path);
      }
      g_free(path);

      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("RustyRig Client Identity", NULL, GTK_DIALOG_MODAL, "Import Existing Key", 1, "Create New Key", 2,
      "Later (legacy login)", GTK_RESPONSE_CANCEL, NULL);
   GtkWidget *label = gtk_label_new("RustyRig uses a private key to identify this computer.\n\n"
      "Create a new identity or import an existing key.\n" "Your private key stays on this computer; only the public key is shared with servers.");
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
   gtk_label_set_max_width_chars(GTK_LABEL(label), 58);
   gtk_widget_set_size_request(label, 360, -1);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 460, -1);
   gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 14);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), label);
   gtk_widget_show_all(dialog);

   for ( ;; ) {
      gint choice = gtk_dialog_run(GTK_DIALOG(dialog));

      if (choice == GTK_RESPONSE_CANCEL || choice == GTK_RESPONSE_DELETE_EVENT) {
         break;
      }
      rr_crypto_key_t *key = NULL;

      if (choice == 2) {
         key = rr_crypto_ed25519_generate();
      } else if (choice == 1) {
         GtkWidget *chooser = gtk_file_chooser_dialog_new("Import Ed25519 Private Key", GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
            GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, NULL);

         if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
            char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
            FILE *f = fopen(filename, "rb");

            if (f) {
               GtkWidget *passdialog = gtk_dialog_new_with_buttons("Import Key Password", GTK_WINDOW(dialog), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL,
                  "_Unlock", GTK_RESPONSE_ACCEPT, NULL);
               GtkWidget *password = gtk_entry_new();
               gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
               gtk_entry_set_placeholder_text(GTK_ENTRY(password), "Password (leave empty for unencrypted key)");
               gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(passdialog))), password);
               gtk_widget_show_all(passdialog);

               if (gtk_dialog_run(GTK_DIALOG(passdialog)) == GTK_RESPONSE_ACCEPT) {
                  const char *pass = gtk_entry_get_text(GTK_ENTRY(password));
                  key = *pass ? rr_crypto_key_read_encrypted_pem(f, pass) : rr_crypto_key_read_encrypted_pem(f, "");
               }
               gtk_widget_destroy(passdialog);
               fclose(f);
            }
            g_free(filename);
         }
         gtk_widget_destroy(chooser);
      }
      unsigned char proof[64];
      const char test_message[] = "rustyrig.client.identity.private-key-check.v1";

      if (key && !rr_crypto_ed25519_sign(key, test_message, sizeof(test_message) - 1, proof)) {
         rr_crypto_key_free(key);
         key = NULL;
      }

      if (key && gtk_identity_save_new(key, path)) {
         Log(LOG_INFO, "client.identity", "Client Ed25519 identity created at %s", path);
         rr_crypto_key_free(key);
         break;
      }
      rr_crypto_key_free(key);
      GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
         "Unable to save a new Ed25519 identity. Check key format and local directory permissions.");
      gtk_dialog_run(GTK_DIALOG(error));
      gtk_widget_destroy(error);
   }

   gtk_widget_destroy(dialog);
   g_free(path);
}

/* Event-bus dispatch bridge. This context is captured on the GTK owner
 * thread rather than looked up from whichever thread emits an event. */
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
   *delivery = (GtkEventDelivery){fn, arg};
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_deliver_event, delivery, g_free);
   g_source_attach(source, context);
   g_source_unref(source);
}

/* Inbound snapshot from the core domain. The payload is owned by the
 * event bus; no connection or GTK object crosses the thread boundary. */
static void frontend_gtk_authoritative_room_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message->payload || !message->length || message->length > 4096) return;
   const char *room = message->payload;
   if (room[message->length - 1] != '\0' ||
       strnlen(room, message->length) != message->length - 1) return;
   gtk_chat_set_authoritative_room(room);
}

static void frontend_gtk_server_picker_show_event(const rr_event_message_t *message, void *unused) {
   (void)message;
   (void)unused;
   if (!dying) gtk_show_server_chooser();
}

/* Existing protocol event, copied by librustyaxe for GTK-domain delivery.
 * No rrconn_t crosses threads. Leave core authoritative state in core. */
/* Subscribe to the original protocol events, with owned JSON delivered by
 * librustyaxe on the GTK domain. The core remains the state authority. */
extern char session_token[HTTP_TOKEN_LEN + 1];
extern const char *login_user;
static void frontend_gtk_room_protocol_event(const char *event, const char *data,
                                              rrconn_t *connection, void *unused) {
   (void)connection;
   (void)unused;
   if (dying || !data) return;
   if (!strcmp(event, "userinfo")) {
      userlist_redraw_gtk();
      return;
   }
   dict *d = json2dict(data);
   if (!d) return;
   const char *room = dict_get(d, "talk.room", NULL);
   if (!room) room = dict_get(d, "talk.target", NULL);
   if (!room) { dict_free(d); return; }
   if (!strcmp(event, "join")) {
      const char *who = dict_get(d, "talk.user", NULL);
      if (who && login_user && !strcasecmp(who, login_user))
         gtk_chat_room_add(room);
      userlist_redraw_gtk();
   } else if (!strcmp(event, "part")) {
      const char *session = dict_get(d, "talk.session", NULL);
      bool self = session && *session && session_token[0] &&
                  !strcmp(session, session_token);
      if (self && !dict_get_bool(d, "talk.kicked", false))
         gtk_chat_room_remove(room);
      userlist_redraw_gtk();
   } else if (!strcmp(event, "room.deleted")) {
      gtk_chat_room_remove(room);
      userlist_redraw_gtk();
   } else if (!strcmp(event, "room.topic")) {
      gtk_chat_room_set_topic(room, dict_get(d, "talk.topic", ""));
   } else if (!strcmp(event, "room.vfo") || !strcmp(event, "room.vfo-list")) {
      gtk_chat_room_vfos_changed(room);
   } else if (!strcmp(event, "room.state")) {
      userlist_room_state_changed(room);
      userlist_redraw_gtk();
      ptt_button_refresh();
   }
   dict_free(d);
}

static void frontend_gtk_talk_msg_event(const char *event, const char *data,
                                         rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;
   if (dying || !data) return;
   dict *d = json2dict(data);
   if (!d) return;
   const char *room = dict_get(d, "talk.target", NULL);
   const char *from = dict_get(d, "talk.from", "?");
   const char *kind = dict_get(d, "talk.msg_type", NULL);
   const char *text = dict_get(d, "talk.data", NULL);
   bool query = dict_get_bool(d, "talk.query", false) ||
      (kind && (!strcasecmp(kind, "priv") || !strcasecmp(kind, "privmsg")));
   if (query && from && login_user && strcasecmp(from, login_user))
      room = from;
   if (room && *room) {
      const char *unread = text && kind && strncasecmp(kind, "replay-", 7) &&
         (!login_user || strcasecmp(from, login_user)) ? text : NULL;
      gtk_chat_incoming(room, query, unread, login_user);
   }
   dict_free(d);
}

static void frontend_gtk_ptt_state_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (message->length != 1 || !message->payload) return;
   const char state = ((const char *)message->payload)[0];
   if (state == '0' || state == '1') {
      g_log("RustyRig-GTK", G_LOG_LEVEL_DEBUG, "PTT state received via GTK event domain: %s", state == '1' ? "ON" : "OFF");
      ptt_button_set_state(state == '1');
   }
}

static void frontend_gtk_ptt_online_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (message->length != 1 || !message->payload) return;
   const char online = ((const char *)message->payload)[0];
   if (online == '0' || online == '1')
      ptt_button_set_online(online == '1');
}

static void frontend_gtk_connection_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message->payload || message->length != sizeof(int)) return;
   int state;
   memcpy(&state, message->payload, sizeof(state));
   rr_connect_button_update_all(state);
}

static void frontend_gtk_vfo_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message->payload || message->length != sizeof(rrclient_vfo_snapshot_t)) return;
   const rrclient_vfo_snapshot_t *snapshot = message->payload;
   /* Fixed-size snapshots contain only bounded, NUL-terminated strings. */
   if (!memchr(snapshot->vfo, 0, sizeof(snapshot->vfo)) ||
       !memchr(snapshot->mode, 0, sizeof(snapshot->mode)) ||
       !memchr(snapshot->room, 0, sizeof(snapshot->room)) ||
       !memchr(snapshot->server, 0, sizeof(snapshot->server))) return;
   kiwi_panel_vfo_snapshot(snapshot);
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
   if (!pending_vfo_paint.source)
      pending_vfo_paint.source = g_idle_add(frontend_gtk_flush_vfo, NULL);
}

static void frontend_gtk_chat_incoming_event(const rr_event_message_t *message, void *unused);
static void frontend_gtk_supervision_event(const rr_event_message_t *message, void *unused);
static void frontend_gtk_admin_result_event(const rr_event_message_t *message, void *unused);
static bool frontend_gtk_init(int *argc, char ***argv) {
   frontend_gtk_owner = g_thread_self();
   frontend_gtk_assert_owner();
   g_log("RustyRig-GTK", G_LOG_LEVEL_MESSAGE, "GTK initialization tid=%p default-context=%p",
       (void *)frontend_gtk_owner, (void *)g_main_context_default());
   gtk_init(argc, argv);
   frontend_gtk_event_context = g_main_context_ref_thread_default();
   if (!event_domain_register("gtk", frontend_gtk_dispatch_event, frontend_gtk_event_context))
      g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to register GTK event delivery domain");
   else
      frontend_gtk_domain_registered = true;
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
   gtk_resources_init();
   gtk_files_init();
   // Local client log pane (GTK log tab) mirrors client logs.
   frontend_gtk_log_callback = log_add_callback_token(log_print_va, LOG_CALLBACK_FILTERED);
   extern bool rr_module_token_add(rr_event_token_t token);
   /* Quit prompts own GTK widgets: dispatch on GTK, not on core. */
   rr_event_token_t quit_event = event_on_domain_token(
      "client.quit.request", frontend_gtk_quit_request, NULL, "gtk");
   if (quit_event) rr_module_token_add(quit_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK quit requests");
   rr_event_token_t action_event = event_on_message_domain_token(
      "client.ui.owner.action", frontend_gtk_owner_action_event, NULL, "gtk");
   if (action_event) rr_module_token_add(action_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to frontend actions");
   rr_event_token_t admin_event = event_on_message_domain_token(
      "client.ui.admin.result", frontend_gtk_admin_result_event, NULL, "gtk");
   if (admin_event) rr_module_token_add(admin_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to admin results");
   rr_event_token_t supervision_event = event_on_message_domain_token(
      "client.ui.supervision.result", frontend_gtk_supervision_event, NULL, "gtk");
   if (supervision_event) rr_module_token_add(supervision_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to supervision results");
   rr_event_token_t roster_redraw_event = event_on_message_domain_token(
      "client.ui.roster.redraw", frontend_gtk_roster_redraw_event, NULL, "gtk");
   if (roster_redraw_event) rr_module_token_add(roster_redraw_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK roster redraws");
   rr_event_token_t incoming_event = event_on_message_domain_token(
      "client.ui.chat.incoming", frontend_gtk_chat_incoming_event, NULL, "gtk");
   if (incoming_event) rr_module_token_add(incoming_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to incoming chat");
   rr_event_token_t room_event = event_on_message_domain_token(
      "client.ui.chat.authoritative-room", frontend_gtk_authoritative_room_event,
      NULL, "gtk");
   if (room_event) rr_module_token_add(room_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to authoritative room events");
   rr_event_token_t picker_event = event_on_message_domain_token(
      "client.ui.server-picker.show", frontend_gtk_server_picker_show_event,
      NULL, "gtk");
   if (picker_event) rr_module_token_add(picker_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to server-picker requests");
   static const char *room_events[] = {
      "join", "part", "room.deleted", "room.topic", "room.vfo",
      "room.vfo-list", "room.state", "userinfo"
   };
   for (size_t i = 0; i < sizeof(room_events) / sizeof(room_events[0]); i++) {
      rr_event_token_t token = event_on_domain_token(
         room_events[i], frontend_gtk_room_protocol_event, NULL, "gtk");
      if (token) rr_module_token_add(token);
      else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to %s on GTK domain", room_events[i]);
   }
   rr_event_token_t talk_event = event_on_domain_token(
      "talk.msg", frontend_gtk_talk_msg_event, NULL, "gtk");
   if (talk_event) rr_module_token_add(talk_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to talk.msg on GTK domain");
   rr_event_token_t ptt_event = event_on_message_domain_token(
      "client.ui.ptt.state", frontend_gtk_ptt_state_event, NULL, "gtk");
   if (ptt_event) rr_module_token_add(ptt_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK PTT state updates");
   rr_event_token_t ptt_online_event = event_on_message_domain_token(
      "client.ui.ptt.online", frontend_gtk_ptt_online_event, NULL, "gtk");
   if (ptt_online_event) rr_module_token_add(ptt_online_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK PTT online updates");
   rr_event_token_t connection_event = event_on_message_domain_token(
      "client.ui.connection.state", frontend_gtk_connection_event, NULL, "gtk");
   if (connection_event) rr_module_token_add(connection_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to GTK connection state");
   rr_event_token_t vfo_event = event_on_message_domain_token(
      RRCLIENT_VFO_SNAPSHOT_EVENT, frontend_gtk_vfo_snapshot_event, NULL, "gtk");
   if (vfo_event) rr_module_token_add(vfo_event);
   else g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to VFO snapshots");
   rr_event_token_t kiwi_status_event = event_on_message_domain_token(
      RRCLIENT_KIWI_STATUS_EVENT, kiwi_panel_status_event, NULL, "gtk");
   if (kiwi_status_event) rr_module_token_add(kiwi_status_event);
   rr_event_token_t local_sdr_event = event_on_message_domain_token(
      RRCLIENT_LOCAL_BRIDGE_ACTIVE_EVENT, kiwi_panel_active_event, NULL, "gtk");
   if (local_sdr_event) rr_module_token_add(local_sdr_event);
   frontend_gtk_update_source = g_timeout_add(1000, frontend_gtk_update_now, NULL);

   return false;
}

/* An orphaned GTK grab can leave the window painting while swallowing all
 * keyboard/mouse input. Never remove live grabs (modal dialogs need them). */
static gboolean frontend_gtk_grab_watchdog(gpointer unused) {
   (void)unused;
   GtkWidget *grab = gtk_grab_get_current();
   if (!grab) return G_SOURCE_CONTINUE;
   if (gtk_widget_get_mapped(grab)) return G_SOURCE_CONTINUE;
   /* A temporarily unmapped widget may become visible in this iteration;
    * only release a grab whose widget is no longer in a visible toplevel. */
   GtkWidget *top = gtk_widget_get_toplevel(grab);
   if (gtk_widget_get_visible(grab) &&
       GTK_IS_WINDOW(top) && gtk_widget_get_mapped(top))
      return G_SOURCE_CONTINUE;
   g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING,
         "Releasing orphaned GTK input grab: widget=%s mapped=%d visible=%d",
         G_OBJECT_TYPE_NAME(grab), gtk_widget_get_mapped(grab),
         gtk_widget_get_visible(grab));
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
   if (gtk_main_level() > 0) gtk_main_quit();
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
typedef struct { char *profile, *nickname; } GtkNickPrompt;
static gboolean frontend_gtk_nick_prompt_deliver(gpointer opaque) {
   GtkNickPrompt *p = opaque;
   if (!dying) frontend_gtk_irc_nick_prompt_owner(p->profile, p->nickname);
   return G_SOURCE_REMOVE;
}
static void frontend_gtk_nick_prompt_free(gpointer opaque) {
   GtkNickPrompt *p = opaque;
   g_free(p->profile);
   g_free(p->nickname);
   g_free(p);
}
static void frontend_gtk_irc_nick_prompt(const char *profile, const char *nickname) {
   if (!profile) return;
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
   } else frontend_gtk_nick_prompt_free(p);
}

/* The event bus owns the cross-thread message copy and GTK dispatch. */
typedef struct {
   uint32_t room_len, text_len, nick_len;
   uint8_t query;
} GtkIncomingChatHeader;
static void frontend_gtk_chat_incoming_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message->payload || message->length < sizeof(GtkIncomingChatHeader)) return;
   GtkIncomingChatHeader header;
   memcpy(&header, message->payload, sizeof(header));
   size_t remaining = message->length - sizeof(header);
   size_t required = (size_t)header.room_len + header.text_len + header.nick_len;
   if (required != remaining || header.room_len == 0 || header.text_len == 0 || header.nick_len == 0) return;
   const char *p = (const char *)message->payload + sizeof(header);
   const char *room = p;
   const char *text = room + header.room_len;
   const char *nickname = text + header.text_len;
   if (room[header.room_len - 1] || text[header.text_len - 1] || nickname[header.nick_len - 1]) return;
   gtk_chat_incoming(*room ? room : NULL, header.query != 0,
                     *text ? text : NULL, *nickname ? nickname : NULL);
}
static void frontend_gtk_chat_incoming(const char *room, bool query,
                                      const char *text, const char *nickname) {
   size_t rl = strlen(room ? room : "") + 1, tl = strlen(text ? text : "") + 1;
   size_t nl = strlen(nickname ? nickname : "") + 1;
   if (rl > UINT32_MAX || tl > UINT32_MAX || nl > UINT32_MAX || rl + tl + nl > 1024 * 1024) return;
   GtkIncomingChatHeader header = { (uint32_t)rl, (uint32_t)tl, (uint32_t)nl, query };
   size_t length = sizeof(header) + rl + tl + nl;
   char *payload = g_malloc(length);
   memcpy(payload, &header, sizeof(header));
   char *p = payload + sizeof(header);
   memcpy(p, room ? room : "", rl); p += rl;
   memcpy(p, text ? text : "", tl); p += tl;
   memcpy(p, nickname ? nickname : "", nl);
   rr_event_message_t message = { .name = "client.ui.chat.incoming",
                                  .payload = payload, .length = length };
   event_emit_message(&message);
   g_free(payload);
}
/* Synchronous frontend getter: never inspect GTK from core. The command
 * callers should eventually consume a core-owned UI state snapshot. */
static bool frontend_gtk_chat_status_active(void) {
   if (g_thread_self() != frontend_gtk_owner) return false;
   return gtk_chat_status_active();
}

extern void gtk_supervision_result_profile(const char *, const char *);
/* Two copied strings; never ship rrconn_t pointers to GTK. */
typedef struct { uint32_t profile_len, data_len; } GtkSupervisionMessage;
static void frontend_gtk_supervision_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (dying || !message->payload || message->length < sizeof(GtkSupervisionMessage)) return;
   GtkSupervisionMessage header;
   memcpy(&header, message->payload, sizeof(header));
   size_t available = message->length - sizeof(header);
   if (!header.profile_len || !header.data_len || header.profile_len > available ||
       header.data_len != available - header.profile_len) return;
   const char *profile = (const char *)message->payload + sizeof(header);
   const char *data = profile + header.profile_len;
   if (profile[header.profile_len - 1] || data[header.data_len - 1]) return;
   gtk_supervision_result_profile(data, profile);
}
static void frontend_gtk_supervision_result(const char *data, rrconn_t *connection) {
   const char *profile = rrclient_connection_name(connection);
   if (!profile || !*profile) return;
   size_t pl = strlen(profile) + 1, dl = strlen(data ? data : "") + 1;
   if (pl > 4096 || dl > 1024 * 1024) return;
   GtkSupervisionMessage header = { (uint32_t)pl, (uint32_t)dl };
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
typedef struct { uint32_t profile_len, data_len; } GtkAdminResultHeader;
static void frontend_gtk_admin_result_event(const rr_event_message_t *message, void *unused);
extern void gtk_admin_result_profile(const char *, const char *);
static void frontend_gtk_admin_result(const char *data, rrconn_t *connection) {
   const char *profile = rrclient_connection_name(connection);
   if (!profile || !*profile) return;
   size_t pl = strlen(profile) + 1, dl = strlen(data ? data : "") + 1;
   if (pl > 4096 || dl > 1024 * 1024) return;
   GtkAdminResultHeader h = {(uint32_t)pl, (uint32_t)dl};
   size_t length = sizeof(h) + pl + dl;
   char *payload = g_malloc(length);
   memcpy(payload, &h, sizeof(h));
   memcpy(payload + sizeof(h), profile, pl);
   memcpy(payload + sizeof(h) + pl, data ? data : "", dl);
   rr_event_message_t event = {.name = "client.ui.admin.result", .payload = payload, .length = length};
   event_emit_message(&event);
   g_free(payload);
}
static void frontend_gtk_admin_result_event(const rr_event_message_t *event, void *unused) {
   (void)unused;
   if (dying || !event->payload || event->length < sizeof(GtkAdminResultHeader)) return;
   GtkAdminResultHeader h;
   memcpy(&h, event->payload, sizeof(h));
   size_t remaining = event->length - sizeof(h);
   if (!h.profile_len || !h.data_len || h.profile_len > remaining ||
       h.data_len != remaining - h.profile_len) return;
   const char *profile = (const char *)event->payload + sizeof(h);
   const char *data = profile + h.profile_len;
   if (profile[h.profile_len - 1] || data[h.data_len - 1]) return;
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
   if (frontend_gtk_stopped) return;
   frontend_gtk_assert_owner();
   frontend_gtk_stopped = true;
   extern void gtk_terminal_shutdown(void);
   gtk_terminal_shutdown();
   gtk_help_shutdown();
   gtk_resources_shutdown();
   gtk_files_shutdown();
   gtk_file_editor_shutdown();
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
      if (!event_domain_unregister("gtk")) return false;
      frontend_gtk_domain_registered = false;
   }
   if (frontend_gtk_event_context) {
      g_main_context_unref(frontend_gtk_event_context);
      frontend_gtk_event_context = NULL;
   }
   return true;
}

void rrclient_gtk_open_key_manager(void) {
   gtk_manage_certificates(NULL, NULL);
}

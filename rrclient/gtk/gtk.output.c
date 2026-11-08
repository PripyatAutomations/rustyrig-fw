// GTK chat/status output with startup backlog and scrollback management.
// Part of rustyrig-fw; GPL-2.0-only.
#include "gtk.core.h"
#include "gtk.chat.h"
#include <librustyaxe/core.h>
#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MSGBUF_SIZE 8096
extern bool dying;

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

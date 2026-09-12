//
// rrclient/gtk.syslog.c
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
#include <librustyaxe/logger.h>

extern dict *cfg;                // config.c
extern time_t now;
extern GtkWidget *main_notebook;

GtkWidget *log_view = NULL;
GtkTextBuffer *log_buffer = NULL;
static GtkWidget *log_page = NULL;       // notebook page holding log_view
static GtkWidget *host_log_page = NULL;  // notebook page holding host_log_view
GtkWidget *host_log_view = NULL;
GtkTextBuffer *host_log_buffer = NULL;
extern bool dying;               // main.c

// Is the client log tab the one the user is looking at?  While it's hidden
// we skip markup colorization, scroll re-arming and other per-line costs;
// plain inserts are still cheap and keep the log complete for when the
// user opens the tab.
static bool log_tab_visible(void) {
   if (!log_page || !main_notebook) {
      return false;
   }

   return (gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook) ) ==
           gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_page) );
}

// When the user switches to a log tab, jump the view to the newest entry.
// log_print_va() skips the per-line scroll idle callback while the tab is
// hidden (the old comment claimed the view "re-scrolls when shown" but nothing
// implemented that), so this signal handler is what actually does it.
// Host log stays plain text (color is a client-side thing); it only gets the
// scroll-on-focus behavior.
static void log_tab_switched(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer user) {
   (void)nb;
   (void)page_num;
   (void)user;

   if (log_view && page == log_page) {
      g_idle_add(ui_scroll_to_end, log_view);
   }

   if (host_log_view && page == host_log_page) {
      g_idle_add(ui_scroll_to_end, host_log_view);
   }
}

// backend
bool log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   if (!fmt || !ap) {
      return true;
   }
   // During shutdown the GTK widgets (and log_buffer) are destroyed before
   // the network/Log() teardown completes; refuse to touch them once dying.
   if (dying || !log_buffer) {
      return true;
   }
   // Respect the configured log level.  Without this we rendered EVERY Log()
   // call (including LOG_CRAZY websocket dumps) into the log tab, which made
   // CPU usage climb the longer the client ran.
   if (debug_filter(subsys, priority)) {
      return true;
   }

      char outbuf[8096];
      memset(outbuf, 0, sizeof(outbuf));
      vsnprintf(outbuf, sizeof(outbuf), fmt, ap);

      bool visible = log_tab_visible();

      GtkTextIter end;

      gtk_text_buffer_get_end_iter(log_buffer, &end);

      if (visible) {
         const char *ts = get_chat_ts(now);
         char *ts_colorized = gtk_colorize_string(ts);

         if (ts_colorized) {
            gtk_text_buffer_insert_markup(log_buffer, &end, ts_colorized, -1);
            free(ts_colorized);
         } else {
            gtk_text_buffer_insert(log_buffer, &end, ts, -1);
         }
      } else {
         // Hidden: plain-text timestamp, no markup parsing
         char tsbuf[32];

         snprintf(tsbuf, sizeof(tsbuf), "%s", get_chat_ts(now) );
         gtk_text_buffer_insert(log_buffer, &end, tsbuf, -1);
      }

      char header[512];
      memset(header, 0, sizeof(header));
      snprintf(header, sizeof(header), " <%s.%s> ", subsys, log_priority_to_str(priority));
      gtk_text_buffer_insert(log_buffer, &end, header, -1);

      if (visible) {
         // Colorize the message body: Log() format strings carry {color} tags
         // (the 03f283d perf change only colorized the timestamp, leaving the
         // body inserted raw with the tags showing literally).
         char *colorized = gtk_colorize_string(outbuf);

         if (colorized) {
            gtk_text_buffer_insert_markup(log_buffer, &end, colorized, -1);
            free(colorized);
         } else {
            gtk_text_buffer_insert(log_buffer, &end, outbuf, -1);
         }
      } else {
         // Hidden: plain text, no markup parsing or colorization cost
         gtk_text_buffer_insert(log_buffer, &end, outbuf, -1);
      }
      gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
      gtk_trim_scrollback(log_buffer, "ui.gtk.scrollback.syslog", 200);

      // Hidden: no scroll idle callback; log_tab_switched() scrolls to the
      // end when the user opens the tab.
      if (visible) {
         g_idle_add(ui_scroll_to_end, log_view);
      }
      return false;
   }

// print to syslog
bool log_print(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   if (!fmt) {
      printf("log_print sent NULL fmt\n");
   }

   // This usually indicates a bug has occurred...
   if (!log_buffer) {
      fprintf(stderr, "log_print called with no log_buffer");
      return false;
   }
   va_list ap;
   va_start(ap, fmt);
   bool rv = log_print_va(priority, subsys, fmt, ap);
   va_end(ap);

   return rv;
}

bool syslog_clear(void) {
   gtk_text_buffer_set_text(log_buffer, "", -1);

   return false;
}

GtkWidget *init_log_tab(void) {
   GtkWidget *nw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   log_view = gtk_text_view_new();
   gtk_widget_set_name(log_view, "log-view");
   log_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(log_view) );
   gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
   // Log font (monospace) comes from the #log-view CSS rule in [gtk-css].
   // Users may want something smaller to fit long lines.
   gtk_container_add(GTK_CONTAINER(nw), log_view);
   GtkWidget *syslog_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(syslog_tab_label), "(<u>4</u>) Client log");
   log_page = nw;
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, syslog_tab_label);

   // Scroll to the end when the user switches to this tab (see log_tab_switched)
   g_signal_connect(main_notebook, "switch-page", G_CALLBACK(log_tab_switched), NULL);

   return nw;
}

GtkWidget *init_host_log_tab(void) {
   GtkWidget *nw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   host_log_view = gtk_text_view_new();
   gtk_widget_set_name(host_log_view, "host-log-view");
   host_log_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(host_log_view) );
   gtk_text_view_set_editable(GTK_TEXT_VIEW(host_log_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(host_log_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(host_log_view), GTK_WRAP_WORD_CHAR);

   // Log font (monospace) comes from the #log-view CSS rule in [gtk-css].
   // Users may want something smaller to fit long lines.
   gtk_container_add(GTK_CONTAINER(nw), host_log_view);
   GtkWidget *syslog_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(syslog_tab_label), "(<u>3</u>) Host Log");
   host_log_page = nw;
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, syslog_tab_label);

   return nw;
}

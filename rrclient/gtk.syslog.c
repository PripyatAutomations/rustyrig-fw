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

extern dict *cfg;                // config.c
extern time_t now;
extern GtkWidget *main_notebook;

GtkTextBuffer *log_buffer = NULL;
GtkWidget *log_view = NULL;

// backend
bool log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   if (!fmt || !ap) {
      return true;
   }

   if (!log_buffer) {
      return true;
   }

   char outbuf[8096];
   memset(outbuf, 0, sizeof(outbuf));
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);

   const char *ts = get_chat_ts(now);
   char *ts_colorized = gtk_colorize_string(ts);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(log_buffer, &end);

   if (ts_colorized) {
      gtk_text_buffer_insert_markup(log_buffer, &end, ts_colorized, -1);
      free(ts_colorized);
   } else {
      gtk_text_buffer_insert(log_buffer, &end, ts, -1);
   }

   char header[512];
   memset(header, 0, sizeof(header));
   snprintf(header, sizeof(header), " <%s.%s> ", subsys, log_priority_to_str(priority));
   gtk_text_buffer_insert(log_buffer, &end, header, -1);
   gtk_text_buffer_insert(log_buffer, &end, outbuf, -1);
   gtk_text_buffer_insert(log_buffer, &end, "\n", 1);
   g_idle_add(ui_scroll_to_end, log_view);
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
   log_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(log_view) );
   gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(nw), log_view);
   GtkWidget *syslog_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(syslog_tab_label), "(<u>3</u>) Syslog");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, syslog_tab_label);

   return nw;
}

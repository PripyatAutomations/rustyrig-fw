//
// gtk-client/gtk.syslog.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern struct mg_mgr mgr;
extern bool ws_connected;
extern bool ws_tx_connected;
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern bool server_ptt_state;
extern const char *tls_ca_path;
extern struct mg_str tls_ca_path_str;
extern bool cfg_show_pings;
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern GtkWidget *notebook;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern dict *servers;
GtkTextBuffer *log_buffer = NULL;
GtkWidget *log_view = NULL;

// print to syslog
bool log_print(const char *fmt, ...) {
   if (!log_buffer) {
      Log(LOG_WARN, "gtk", "log_print called with no log_buffer");
      return false;
   }

   if (!fmt) {
      printf("log_print sent NULL fmt\n");
   }
   va_list ap;
   va_start(ap, fmt);
   char outbuf[8096];
   vsnprintf(outbuf, sizeof(outbuf), fmt, ap);
   va_end(ap);

   GtkTextIter end;
   gtk_text_buffer_get_end_iter(log_buffer, &end);
   gtk_text_buffer_insert(log_buffer, &end, outbuf, -1);
   gtk_text_buffer_insert(log_buffer, &end, "\n", 1);

   gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log_view), &end, 0.0, FALSE, 0.0, 0.0);
   return true;
}

bool clear_syslog(void) {
   gtk_text_buffer_set_text(log_buffer, "", -1);
   return false;
}

GtkWidget *init_log_tab(void) {
   GtkWidget *nw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   log_view = gtk_text_view_new();
   log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
   gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
   gtk_container_add(GTK_CONTAINER(nw), log_view);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), nw, gtk_label_new("Log"));
   return nw;
}

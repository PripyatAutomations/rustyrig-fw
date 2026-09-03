//
// rrclient/chat.whois.c: Chat related stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)
extern rrconn_t *ws_conn;

extern dict *cfg;

void ui_show_whois_dialog(GtkWindow *parent, dict *d) {
   if (!parent || !d) {
      return;
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Whois Info", parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);

   gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

   GtkWidget *content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );
   GtkWidget *label = gtk_label_new(NULL);
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
   gtk_label_set_selectable(GTK_LABEL(label), TRUE);
   char *markup = NULL;
   size_t len = 0;

   FILE *stream = open_memstream(&markup, &len);
   if (!stream) {
      Log(LOG_CRIT, "rrproto", "ui_show_whois_dialog: unable to open_memstream()!: %d:%s", errno, strerror(errno));
      return;
   }
   int idx = 0;
   const char *s_none = "(none)";
   const char *s_unknown = "(unknown)";
   const char *username = dict_get(d, "talk.username", s_none);
   const char *email = dict_get(d, "talk.email", s_none);
   const char *privs = dict_get(d, "talk.privs", s_none);
   const char *ua = dict_get(d, "talk.ua", s_unknown);
   bool muted = dict_get_bool(d, "talk.muted", false);
   long connected = dict_get_long(d, "talk.connected", 0);
   long last_heard = dict_get_long(d, "talk.last_heard", 0);
   int clones = dict_get_int(d, "talk.clones", 0);

   fprintf(stream, "<b>User:</b> %s\n"
      "<b>Email:</b> %s\n"
      "<b>Privileges:</b> %s\n"
      "%s"
      "<b>Clones:</b> #%d\n"
      "<b>Connected:</b> %s\n"
      "<b>Last Heard:</b> %s\n"
      "<b>User-Agent:</b> <tt>%s</tt>\n"
      "<hr/>\n", username, email, privs,
      (muted ? "<span foreground=\"red\"><b>This user is muted.</b></span>\n" : ""),
      clones, ctime( (time_t*)&connected ), ctime( (time_t*)&last_heard ), ua);
   fclose(stream);

   gtk_label_set_markup(GTK_LABEL(label), markup);
   gtk_container_add(GTK_CONTAINER(content_area), label);
   gtk_widget_show_all(dialog);

   Log(LOG_DEBUG, "gtk", "Connect whois callback response");
   g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
   free(markup);
}

bool cmd_whois(int argc, char **args) {
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "whois");
   dict_add(d, "talk.args", args[1]);
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

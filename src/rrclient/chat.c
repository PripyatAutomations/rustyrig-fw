#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "../ext/libmongoose/mongoose.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern dict *cfg;
extern bool ws_connected;
extern time_t now;
extern bool ptt_active;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *create_user_list_window(void);
extern time_t poll_block_expire, poll_block_delay;
extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);

void ui_show_whois_dialog(GtkWindow *parent, const char *json_array) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Whois Info",
      parent,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Close", GTK_RESPONSE_CLOSE,
      NULL
   );

   gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

   GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *label = gtk_label_new(NULL);
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
   gtk_label_set_selectable(GTK_LABEL(label), TRUE);
   char *markup = NULL;
   size_t len = 0;
/*
   FILE *stream = open_memstream(&markup, &len);

   int idx = 0;
#if	0	// XXX: clean this up
      const char *username = mg_json_get_str(elem, "$.username")
      const char *email = (strlen(v = mg_json_get_str(elem, "$.email")) ? v : "(none)");
      const char *privs = (strlen(v = mg_json_get_str(elem, "$.privs")) ? v : "None");
      const char *ua = (strlen(v = mg_json_get_str(elem, "$.ua")) ? v : "Unknown");
      const char *muted = (strlen(v = mg_json_get_str(elem, "$.muted")) ? v : "false");

      long connected = mg_json_get_long(elem, "$.connected", 0);
      long last_heard = mg_json_get_long(elem, "$.last_heard", 0);
      int clones = (int) mg_json_get_long(elem, "$.clones", 0);

      fprintf(stream,
         "<b>User:</b> %s\n"
         "<b>Email:</b> %s\n"
         "<b>Privileges:</b> %s\n"
         "%s"
         "<b>Clones:</b> #%d\n"
         "<b>Connected:</b> %s\n"
         "<b>Last Heard:</b> %s\n"
         "<b>User-Agent:</b> <tt>%s</tt>\n"
         "<hr/>\n",
         username,
         email,
         privs,
         (strcmp(muted, "true") == 0) ? "<span foreground=\"red\"><b>This user is muted.</b></span>\n" : "",
         clones,
         ctime((time_t*)&connected),
         ctime((time_t*)&last_heard),
         ua
      );
   }
#endif

   fclose(stream);
*/  
   gtk_label_set_markup(GTK_LABEL(label), markup);
   gtk_container_add(GTK_CONTAINER(content_area), label);
   gtk_widget_show_all(dialog);
   Log(LOG_DEBUG, "gtk", "Connect whois callback response");
   g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
   free(markup);
}

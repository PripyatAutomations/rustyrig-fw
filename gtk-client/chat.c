#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
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
   FILE *stream = open_memstream(&markup, &len);

   int idx = 0;
#if	0	// XXX: clean this up
   struct mg_str json = mg_str(json_array);
   struct mg_str elem;
   while ((elem = mg_json_get_arr(json, idx++)).len > 0) {
      struct mg_str v;

      const char *username = (v = mg_json_get_str(elem, "$.username")).len ? v.ptr : "Unknown";
      const char *email = (v = mg_json_get_str(elem, "$.email")).len ? v.ptr : "(none)";
      const char *privs = (v = mg_json_get_str(elem, "$.privs")).len ? v.ptr : "None";
      const char *ua = (v = mg_json_get_str(elem, "$.ua")).len ? v.ptr : "Unknown";
      const char *muted = (v = mg_json_get_str(elem, "$.muted")).len ? v.ptr : "false";

      long connected = mg_json_get_long(elem, "$.connected", 0);
      long last_heard = mg_json_get_long(elem, "$.last_heard", 0);
      int clone = (int) mg_json_get_long(elem, "$.clone", 0);

      fprintf(stream,
         "<b>User:</b> %s\n"
         "<b>Email:</b> %s\n"
         "<b>Privileges:</b> %s\n"
         "%s"
         "<b>Clone:</b> #%d\n"
         "<b>Connected:</b> %s\n"
         "<b>Last Heard:</b> %s\n"
         "<b>User-Agent:</b> <tt>%s</tt>\n"
         "<hr/>\n",
         username,
         email,
         privs,
         (strcmp(muted, "true") == 0) ? "<span foreground=\"red\"><b>This user is muted.</b></span>\n" : "",
         clone,
         ctime((time_t*)&connected),
         ctime((time_t*)&last_heard),
         ua
      );
   }
#endif

   fclose(stream);
   gtk_label_set_markup(GTK_LABEL(label), markup);
   gtk_container_add(GTK_CONTAINER(content_area), label);
   gtk_widget_show_all(dialog);
   g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
   free(markup);
}

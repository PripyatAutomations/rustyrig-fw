#include <gtk/gtk.h>
#include <stdbool.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.admin.sessions.h>

void gtk_admin_session_set_sender(bool (*sender)(admin_table_t *, const char *, const char *));
static bool (*admin_session_sender)(admin_table_t *, const char *, const char *);

static bool admin_session_send(admin_table_t *table, const char *command, const char *id) {
   if (!admin_session_sender) {
      return false;
   }

   return admin_session_sender(table, command, id);
}

void gtk_admin_session_set_sender(bool (*sender)(admin_table_t *, const char *, const char *)) {
   admin_session_sender = sender;
}

void gtk_admin_session_kick(GtkButton *button, gpointer data) {
   admin_table_t *table = data;
   bool all = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "kick-all"));
   char *id = NULL, *username = NULL, *kickable = NULL;

   if (!all) {
      GtkTreeModel *model = NULL;
      GtkTreeIter iter;

      if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), &model, &iter)) {
         return;
      }
      gtk_tree_model_get(model, &iter, 0, &id, 1, &username, 6, &kickable, -1);

      if (g_strcmp0(kickable, "yes")) {
         gtk_label_set_text(GTK_LABEL(table->status), "This session is protected or belongs to your current login.");
         g_free(id);
         g_free(username);
         g_free(kickable);

         return;
      }
   }
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_OK_CANCEL, all ? "Disconnect all permitted sessions on %s?" : "Disconnect %s on %s?", all ? table->profile : username ? username :
      "selected session", all ? "" : table->profile);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The server will exclude your session and protected accounts. "
      "Active radio transmissions must be released by normal disconnect handling.");

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      admin_session_send(table, all ? "session-kick-all" : "session-kick", all ? NULL : id);
   }
   gtk_widget_destroy(dialog);
   g_free(id);
   g_free(username);
   g_free(kickable);
}

void gtk_admin_session_selection(GtkTreeSelection *selection, gpointer user) {
   GtkWidget *button=GTK_WIDGET(user);
   gtk_widget_set_sensitive(button,gtk_tree_selection_count_selected_rows(selection)>0);
}

#include <gtk/gtk.h>
#include <stdbool.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.admin.sessions.h>

void gtk_admin_session_set_sender(bool (*sender) (admin_table_t *, const char *, const char *));
static bool (*admin_session_sender)(admin_table_t *, const char *, const char *);

static bool admin_session_send(admin_table_t *table, const char *command, const char *id) {
   if (!admin_session_sender) {
      return false;
   }

   return admin_session_sender(table, command, id);
}

void gtk_admin_session_set_sender(bool (*sender) (admin_table_t *, const char *, const char *)) {
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
   GtkWidget *button = GTK_WIDGET(user);
   gtk_widget_set_sensitive(button, gtk_tree_selection_count_selected_rows(selection) > 0);
}

/* The server decides what session fields an operator may see. This dialog
 * displays only the already-authorized row, never queries hidden metadata. */
static void admin_session_info(admin_table_t *table) {
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;
   if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(table->tree)), &model, &iter)) return;
   static const char *labels[] = {
      "Session ID", "Username", "IP address", "Client", "Server",
      "Connected (Unix timestamp)", "Can disconnect", "Serial ports"
   };
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Session information", GTK_WINDOW(main_window),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 7);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
   for (int i = 0; i < 8; ++i) {
      char *value = NULL;
      gtk_tree_model_get(model, &iter, i, &value, -1);
      GtkWidget *label = gtk_label_new(labels[i]);
      GtkWidget *text = gtk_label_new(value && *value ? value : "Unavailable");
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_label_set_xalign(GTK_LABEL(text), 0);
      gtk_label_set_selectable(GTK_LABEL(text), TRUE);
      gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), text, 1, i, 1, 1);
      g_free(value);
   }
   gtk_widget_show_all(dialog);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

void gtk_admin_session_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data) {
   (void)tree; (void)path; (void)column;
   admin_session_info(data);
}

static void admin_session_menu_info(GtkMenuItem *item, gpointer data) {
   (void)item;
   admin_session_info(data);
}
static void admin_session_menu_kick(GtkMenuItem *item, gpointer data) {
   admin_table_t *table = data;
   (void)item;
   GtkWidget *button = gtk_button_new();
   gtk_admin_session_kick(GTK_BUTTON(button), table);
   gtk_widget_destroy(button);
}
gboolean gtk_admin_session_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
   GtkTreePath *path = NULL;
   if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
      return FALSE;
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
   gtk_tree_selection_select_path(selection, path);
   gtk_tree_path_free(path);
   GtkWidget *menu = gtk_menu_new();
   GtkWidget *info = gtk_menu_item_new_with_label("Session info");
   GtkWidget *kick = gtk_menu_item_new_with_label("Kick user");
   char *allowed = NULL;
   GtkTreeModel *model = NULL;
   GtkTreeIter iter;
   if (gtk_tree_selection_get_selected(selection, &model, &iter))
      gtk_tree_model_get(model, &iter, 6, &allowed, -1);
   gtk_widget_set_sensitive(kick, allowed && !g_ascii_strcasecmp(allowed, "yes"));
   g_free(allowed);
   g_signal_connect(info, "activate", G_CALLBACK(admin_session_menu_info), data);
   g_signal_connect(kick, "activate", G_CALLBACK(admin_session_menu_kick), data);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), info);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), kick);
   gtk_widget_show_all(menu);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
   return TRUE;
}

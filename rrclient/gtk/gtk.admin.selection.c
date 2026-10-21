// Administrative table filter and selection presentation callbacks.
#include <gtk/gtk.h>
#include <string.h>
#include <rrclient/gtk/gtk.admin.cells.h>
#include <rrclient/gtk/gtk.admin.selection.h>

gboolean gtk_admin_filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
   GtkWidget *entry = GTK_WIDGET(data);
   const char *needle = gtk_entry_get_text(GTK_ENTRY(entry));

   if (!needle || !*needle) {
      return TRUE;
   }
   char *search = g_utf8_casefold(needle, -1);
   gboolean match = FALSE;

   for (int c = 0 ; c < 10 && !match ; c++) {
      char *cell = NULL;
      gtk_tree_model_get(model, iter, c, &cell, -1);

      if (cell) {
         char *folded = g_utf8_casefold(cell, -1);
         match = folded && strstr(folded, search) != NULL;
         g_free(folded);
         g_free(cell);
      }
   }

   g_free(search);

   return match;
}
void gtk_admin_filter_changed(GtkEditable *editable, gpointer data) {
   (void)editable;
   gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(data));
}
void gtk_admin_privilege_selection(GtkTreeSelection *selection, gpointer user) {
   GtkWidget *box = GTK_WIDGET(user);
   gboolean selected = gtk_tree_selection_count_selected_rows(selection) > 0;
   GtkWidget *remove = g_object_get_data(G_OBJECT(box), "rr-privilege-selection-button");
   GtkWidget *edit = g_object_get_data(G_OBJECT(box), "rr-privilege-edit-button");

   if (remove) {
      gtk_widget_set_sensitive(remove, selected);
   }

   if (edit) {
      gtk_widget_set_sensitive(edit, selected);
   }
}
void gtk_admin_action_selection(GtkTreeSelection *selection, gpointer user) {
   admin_table_t *table = user;
   GPtrArray *buttons = g_object_get_data(G_OBJECT(table->tree), "selection-buttons");
   GtkTreeIter iter;
   GtkTreeModel *model = NULL;
   gboolean selected = gtk_tree_selection_get_selected(selection, &model, &iter);
   gboolean deleted = FALSE;

   if (selected && !strcmp(table->name, "rooms")) {
      char *state = NULL;
      gtk_tree_model_get(model, &iter, 2, &state, -1);
      deleted = state && (!strcmp(state, "1") || !g_ascii_strcasecmp(state, "true"));
      g_free(state);
   }

   if (!buttons) {
      return;
   }

   for (guint i = 0 ; i < buttons->len ; ++i) {
      GtkWidget *button = g_ptr_array_index(buttons, i);
      const char *action = g_object_get_data(G_OBJECT(button), "action");
      gtk_widget_set_sensitive(button, selected && !(deleted && action && !strcmp(action, "edit")));
   }
}

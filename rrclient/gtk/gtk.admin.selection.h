#ifndef RRCLIENT_GTK_ADMIN_SELECTION_H
#define RRCLIENT_GTK_ADMIN_SELECTION_H
#include <gtk/gtk.h>

gboolean gtk_admin_filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
void gtk_admin_filter_changed(GtkEditable *editable, gpointer data);
void gtk_admin_privilege_selection(GtkTreeSelection *selection, gpointer user);
void gtk_admin_action_selection(GtkTreeSelection *selection, gpointer user);

#endif

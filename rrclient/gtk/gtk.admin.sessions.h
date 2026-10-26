#ifndef RRCLIENT_GTK_ADMIN_SESSIONS_H
#define RRCLIENT_GTK_ADMIN_SESSIONS_H
#include <gtk/gtk.h>
#include <stdbool.h>
#include <rrclient/gtk/gtk.admin.cells.h>

void gtk_admin_session_set_sender(bool (*sender) (admin_table_t *, const char *, const char *));
void gtk_admin_session_kick(GtkButton *button, gpointer data);
void gtk_admin_session_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data);
gboolean gtk_admin_session_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data);
void gtk_admin_session_selection(GtkTreeSelection *selection, gpointer user);

#endif

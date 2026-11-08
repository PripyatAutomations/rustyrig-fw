// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_ADMIN_PRIVGROUP_H
#define RRCLIENT_GTK_ADMIN_PRIVGROUP_H
#include <gtk/gtk.h>
#include <stdbool.h>
#include <rrclient/gtk/gtk.admin.cells.h>

void gtk_admin_privgroup_set_sender(bool (*sender) (admin_table_t *, const char *, const char *, const char *));
void gtk_admin_privgroup_edit(GtkButton *button, gpointer user);
void gtk_admin_privgroup_open_clicked(GtkButton *button, gpointer user);
void gtk_admin_privgroup_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user);
gboolean gtk_admin_privgroup_context_menu(GtkWidget *tree, GdkEventButton *event, gpointer user);
#endif

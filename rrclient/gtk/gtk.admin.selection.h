// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_ADMIN_SELECTION_H
#define RRCLIENT_GTK_ADMIN_SELECTION_H
#include <gtk/gtk.h>

gboolean gtk_admin_filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
void gtk_admin_filter_changed(GtkEditable *editable, gpointer data);
void gtk_admin_privilege_selection(GtkTreeSelection *selection, gpointer user);
void gtk_admin_action_selection(GtkTreeSelection *selection, gpointer user);

#endif

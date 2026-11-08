// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// GTK privilege selection; server validation remains authoritative.
#ifndef RRCLIENT_GTK_PRIVILEGES_H
#define RRCLIENT_GTK_PRIVILEGES_H
#include <gtk/gtk.h>
/* Returns a newly allocated list on Apply, NULL on Cancel. Preserves unknown grants. */
char *gtk_edit_privileges(GtkWindow *parent, const char *server, const char *name, const char *current);
/* Same checklist, with group-specific header and no nested group selection. */
char *gtk_edit_group_privileges(GtkWindow *parent, const char *server, const char *group, const char *current);
#endif

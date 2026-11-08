// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_ADMIN_ACCESS_H
#define RRCLIENT_GTK_ADMIN_ACCESS_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

GtkWidget *gtk_admin_access_page(GtkListStore *groups);
GtkWidget *gtk_admin_access_label(void);
void gtk_admin_access_refresh(void);
bool gtk_admin_access_result(const char *name, const char *action, dict *row, const char *profile);
#endif

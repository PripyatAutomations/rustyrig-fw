// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_ADMIN_COMMAND_H
#define RRCLIENT_GTK_ADMIN_COMMAND_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>

bool gtk_admin_command_event(GtkWidget *status, const char *profile, const char *command, const char *tail, const char *target, dict *fields);
const char *gtk_admin_selected_profile(void);

#endif

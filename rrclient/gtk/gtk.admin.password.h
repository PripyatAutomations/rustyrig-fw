// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_ADMIN_PASSWORD_H
#define RRCLIENT_GTK_ADMIN_PASSWORD_H
#include <gtk/gtk.h>

void gtk_admin_password_dialog(GtkWindow *parent, const char *profile, const char *name, void (*submit) (void *context, const char *tail), void *context);
#endif

// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Native local serial endpoint presentation; common sercom owns connections.
#ifndef RRCLIENT_GTK_SERIAL_H
#define RRCLIENT_GTK_SERIAL_H
#include <stdbool.h>
#include <gtk/gtk.h>
/* Owned sorted strings; release with g_list_free_full(names, g_free). */
GList *gtk_serial_configured_names(void);
bool gtk_serial_attach(GtkWindow *parent, const char *profile, const char *service);
/* Return the attached endpoint name when the caller wants to open a console. */
char *gtk_serial_attach_named(GtkWindow *parent, const char *profile, const char *service);
void gtk_serial_configure(GtkWindow *parent, const char *name);
#endif

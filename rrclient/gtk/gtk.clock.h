// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_CLOCK_H
#define RRCLIENT_GTK_CLOCK_H
#include <gtk/gtk.h>
void gtk_clock_init(void);
void gtk_clock_shutdown(void);
GtkWidget *gtk_clock_status_new(void);
#endif

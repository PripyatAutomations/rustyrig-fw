// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_SPECTRUM_H
#define RRCLIENT_GTK_SPECTRUM_H
#include <gtk/gtk.h>
/* A shallow room-owned, renderer-selectable AF visualization. */
GtkWidget *rr_gtk_rf_spectrum_new(void);
GtkWidget *rr_gtk_spectrum_panel_new(const char *server, const char *room);
#endif

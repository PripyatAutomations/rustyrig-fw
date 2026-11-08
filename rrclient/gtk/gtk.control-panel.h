// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
// Reusable draggable floating control panel inside the main GTK window.
#ifndef RRCLIENT_GTK_CONTROL_PANEL_H
#define RRCLIENT_GTK_CONTROL_PANEL_H
#include <gtk/gtk.h>
/* Returns a content box; destroying its enclosing panel closes it. */
GtkWidget *gtk_control_panel_open(const char *title, GtkWidget **panel_out);
#endif

// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RRCLIENT_GTK_CONNECT_BUTTON_H
#define RRCLIENT_GTK_CONNECT_BUTTON_H
#include <gtk/gtk.h>
/* Independent, embeddable connection control. Returned widget is GTK-owned. */
GtkWidget *rr_connect_button_new(void);
void rr_connect_button_set_state(GtkWidget *button, int connected);
void rr_connect_button_update_all(int connected);
#endif

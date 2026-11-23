// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_GTK_MODEM_TERMINAL_H
#define RR_GTK_MODEM_TERMINAL_H
#include <gtk/gtk.h>
#include <rrclient/local.modem.h>
void rr_gtk_modem_terminal_init(GtkWidget *notebook);
void rr_gtk_modem_terminal_sync(const rr_local_modem_entry_t *entry);
void rr_gtk_modem_terminal_shutdown(void);
#endif

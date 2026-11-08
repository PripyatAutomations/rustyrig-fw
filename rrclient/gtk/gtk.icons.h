//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only

#ifndef RRCLIENT_GTK_ICONS_H
#define RRCLIENT_GTK_ICONS_H
#include <stdbool.h>
#include <gtk/gtk.h>
/* Set this window's icon and application defaults inherited by auxiliary windows. */
bool set_window_icon(GtkWidget *window, const char *icon_name);
#endif

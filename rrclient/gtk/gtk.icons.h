//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#ifndef RRCLIENT_GTK_ICONS_H
#define RRCLIENT_GTK_ICONS_H
#include <stdbool.h>
#include <gtk/gtk.h>
/* Set this window's icon and application defaults inherited by auxiliary windows. */
bool set_window_icon(GtkWidget *window, const char *icon_name);
#endif

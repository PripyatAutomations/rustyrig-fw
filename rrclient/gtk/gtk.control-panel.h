// Reusable draggable floating control panel inside the main GTK window.
#ifndef RRCLIENT_GTK_CONTROL_PANEL_H
#define RRCLIENT_GTK_CONTROL_PANEL_H
#include <gtk/gtk.h>
/* Returns a content box; destroying its enclosing panel closes it. */
GtkWidget *gtk_control_panel_open(const char *title, GtkWidget **panel_out);
#endif

#ifndef RRCLIENT_GTK_TABS_H
#define RRCLIENT_GTK_TABS_H
#include <gtk/gtk.h>
GtkWidget *gtk_wrapping_tabs_new(GtkNotebook *notebook, GtkPositionType position);
gboolean gtk_tabs_select(GtkNotebook *notebook, int number);
gboolean gtk_tabs_step(GtkNotebook *notebook, int direction);
#endif

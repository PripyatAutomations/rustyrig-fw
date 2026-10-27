#ifndef RRCLIENT_GTK_SPECTRUM_H
#define RRCLIENT_GTK_SPECTRUM_H
#include <gtk/gtk.h>
/* A shallow room-owned, renderer-selectable AF visualization. */
GtkWidget *rr_gtk_spectrum_panel_new(const char *server, const char *room);
#endif

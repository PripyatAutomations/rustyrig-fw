#ifndef RRCLIENT_GTK_CLOCK_H
#define RRCLIENT_GTK_CLOCK_H
#include <gtk/gtk.h>
void gtk_clock_init(void);
void gtk_clock_shutdown(void);
GtkWidget *gtk_clock_status_new(void);
#endif

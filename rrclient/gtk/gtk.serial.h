// Native local serial endpoint presentation; common sercom owns connections.
#ifndef RRCLIENT_GTK_SERIAL_H
#define RRCLIENT_GTK_SERIAL_H
#include <stdbool.h>
#include <gtk/gtk.h>
/* Owned sorted strings; release with g_list_free_full(names, g_free). */
GList *gtk_serial_configured_names(void);
bool gtk_serial_attach(GtkWindow *parent, const char *profile, const char *service);
void gtk_serial_configure(GtkWindow *parent, const char *name);
#endif

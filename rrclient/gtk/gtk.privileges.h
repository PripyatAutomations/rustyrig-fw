// GTK privilege selection; server validation remains authoritative.
#ifndef RRCLIENT_GTK_PRIVILEGES_H
#define RRCLIENT_GTK_PRIVILEGES_H
#include <gtk/gtk.h>
/* Returns a newly allocated list on Apply, NULL on Cancel. Preserves unknown grants. */
char *gtk_edit_privileges(GtkWindow *parent, const char *server, const char *name, const char *current);
#endif

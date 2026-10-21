#ifndef RRCLIENT_GTK_ADMIN_PASSWORD_H
#define RRCLIENT_GTK_ADMIN_PASSWORD_H
#include <gtk/gtk.h>

void gtk_admin_password_dialog(GtkWindow *parent, const char *profile, const char *name,
                               void (*submit)(void *context, const char *tail), void *context);
#endif

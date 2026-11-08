#ifndef RRCLIENT_GTK_ADMIN_SECURITY_H
#define RRCLIENT_GTK_ADMIN_SECURITY_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
GtkWidget *gtk_admin_security_page(void);
void gtk_admin_security_refresh(void);
bool gtk_admin_security_result(const char *name, const char *action, dict *row, const char *profile);
#endif

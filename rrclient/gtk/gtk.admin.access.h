#ifndef RRCLIENT_GTK_ADMIN_ACCESS_H
#define RRCLIENT_GTK_ADMIN_ACCESS_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

GtkWidget *gtk_admin_access_page(GtkListStore *groups);
GtkWidget *gtk_admin_access_label(void);
void gtk_admin_access_refresh(void);
bool gtk_admin_access_result(const char *name, const char *action, dict *row, rrconn_t *connection);
#endif

#ifndef RRCLIENT_GTK_ADMIN_CONTROLS_H
#define RRCLIENT_GTK_ADMIN_CONTROLS_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

GtkWidget *gtk_admin_controls_page(void);
void gtk_admin_controls_refresh(void);
bool gtk_admin_controls_result(const char *name, const char *action, dict *row, rrconn_t *connection);
void gtk_admin_open_control_on_server(const char *profile, const char *name);
void gtk_admin_open_control(const char *name);

#endif

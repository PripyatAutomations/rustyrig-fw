#ifndef RRCLIENT_GTK_ADMIN_COMMAND_H
#define RRCLIENT_GTK_ADMIN_COMMAND_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>

bool gtk_admin_command_event(GtkWidget *status, const char *profile, const char *command, const char *tail, const char *target, dict *fields);
const char *gtk_admin_selected_profile(void);

#endif

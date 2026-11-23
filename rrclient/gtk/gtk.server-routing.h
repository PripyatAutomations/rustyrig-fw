// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_GTK_SERVER_ROUTING_H
#define RR_GTK_SERVER_ROUTING_H
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
GtkWidget *gtk_server_routing_page(void);
GtkWidget *gtk_server_pipelines_page(void);
GtkWidget *gtk_server_routing_server_selector(void);
gboolean gtk_server_routing_result(const char *table,const char *action,
                                   dict *row,const char *profile);
#endif

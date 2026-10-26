#ifndef RRCLIENT_GTK_LOGVIEW_H
#define RRCLIENT_GTK_LOGVIEW_H
#include <gtk/gtk.h>
#include <librustyaxe/logger.h>

typedef struct GtkLogView GtkLogView;
GtkLogView *gtk_logview_new(unsigned limit);
GtkWidget *gtk_logview_widget(GtkLogView *view);
void gtk_logview_append(GtkLogView *view, gboolean host, const char *source,
   logpriority_t level, const char *subsys, const char *message);
void gtk_logview_clear(GtkLogView *view);
#endif

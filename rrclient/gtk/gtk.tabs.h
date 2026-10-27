#ifndef RRCLIENT_GTK_TABS_H
#define RRCLIENT_GTK_TABS_H
#include <gtk/gtk.h>
/* A page opts in only when its owning conversation supports closing. */
typedef void (*GtkTabCloseFunc)(GtkWidget *page, gboolean force);
void gtk_tabs_page_set_close_func(GtkWidget *page, GtkTabCloseFunc function);
void gtk_tabs_page_set_topic(GtkNotebook *notebook, GtkWidget *page, const char *topic);
void gtk_tabs_page_activity(GtkNotebook *notebook, GtkWidget *page, gboolean mention);
void gtk_tabs_bind_menu(GtkWidget *widget, GtkNotebook *notebook, GtkWidget *page);
GtkWidget *gtk_wrapping_tabs_new(GtkNotebook *notebook, GtkPositionType position);
gboolean gtk_tabs_select(GtkNotebook *notebook, int number);
gboolean gtk_tabs_step(GtkNotebook *notebook, int direction);
GtkWidget *gtk_tabs_panel_restore_add(GtkNotebook *notebook, const char *title, GCallback callback, gpointer user_data);
void gtk_tabs_panel_restore_remove(GtkWidget *tab);
#endif

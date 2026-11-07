#ifndef RRCLIENT_GTK_SCALE_CONTAINER_H
#define RRCLIENT_GTK_SCALE_CONTAINER_H
#include <gtk/gtk.h>
/* Scale the native GTK widget subtree without replacing its input windows. This preserves normal focus, selection, scrolling and popovers. */
GtkWidget *rr_scale_container_new(GtkWidget *content);
void rr_scale_container_set_zoom(GtkWidget *wrapper, double scale);
#endif

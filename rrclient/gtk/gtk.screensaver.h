#ifndef RR_GTK_SCREENSAVER_H
#define RR_GTK_SCREENSAVER_H
#include <gtk/gtk.h>
gboolean gtk_screensaver_activity_event(GtkWidget *widget, GdkEvent *event, gpointer data);
gboolean gtk_screensaver_idle_check(gpointer unused);
gboolean gtk_screensaver_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
void gtk_screensaver_activity(void);
#endif

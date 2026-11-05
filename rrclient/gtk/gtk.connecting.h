#ifndef RR_GTK_CONNECTING_H
#define RR_GTK_CONNECTING_H
#include <gtk/gtk.h>
GtkWidget *rr_connect_dialog_new(GtkWindow *parent);
void rr_connect_dialog_set_step(GtkWidget *dialog, unsigned step);
void rr_connect_dialog_set_status(GtkWidget *dialog, const char *status);
void gtk_connecting_init(void);
void gtk_connecting_shutdown(void);
#endif

#ifndef RR_GTK_IDENTITY_H
#define RR_GTK_IDENTITY_H
#include <gtk/gtk.h>
GtkWidget *gtk_certificates_button_new(void);
void gtk_identity_first_run(void);
void rrclient_gtk_open_key_manager(void);
#endif

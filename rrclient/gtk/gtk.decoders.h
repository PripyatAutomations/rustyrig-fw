// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_GTK_DECODERS_H
#define RR_GTK_DECODERS_H
#include <gtk/gtk.h>
void gtk_decoder_view_init(GtkWidget *notebook,GtkWidget *toolbar);
void gtk_decoder_view_shutdown(void);
void gtk_decoders_show(void);
void gtk_decoders_shutdown(void);
#endif

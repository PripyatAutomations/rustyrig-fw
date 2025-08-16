//
// rrclient/gtk.vol-box.c: Transmit power
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"

GtkWidget *rx_vol_slider = NULL;

void on_rx_volume_changed(GtkRange *range, gpointer user_data) {
   gdouble val = gtk_range_get_value(range);
   val /= 100.0;  // scale from 0–100 to 0.0–1.0
   g_object_set(G_OBJECT(user_data), "volume", val, NULL);
}

GtkWidget *create_volbox(void) {
   // RX VOLUME Box
   GtkWidget *rx_vol_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *rx_vol_label = gtk_label_new("RX Vol");
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_widget_set_tooltip_text(rx_vol_slider, "Receive Volume");
   gtk_scale_set_digits(GTK_SCALE(rx_vol_slider), 0);

   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_label, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_slider, TRUE, TRUE, 0);

   int cfg_def_vol_rx = cfg_get_int("audio.volume.rx", 0);
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), cfg_def_vol_rx);
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(on_rx_volume_changed), rx_vol_gst_elem);
   return rx_vol_vbox;
}

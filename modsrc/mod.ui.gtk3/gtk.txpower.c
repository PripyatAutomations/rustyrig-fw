//
// rrclient/gtk.txpower.c: Transmit
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/config.h>
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
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"

GtkWidget *tx_power_slider = NULL;

GtkWidget *create_txpower_box(void) {
   GtkWidget *tx_power_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *tx_power_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(tx_power_label), "TX <u>P</u>ower");
   tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_widget_set_tooltip_text(tx_power_slider, "Transmit Power (0-100%)");
   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_label, TRUE, TRUE, 1);
   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_slider, TRUE, TRUE, 1);
   int cfg_def_pow_tx = cfg_get_int("default.tx.power", 0);
   gtk_range_set_value(GTK_RANGE(tx_power_slider), cfg_def_pow_tx);

   return tx_power_vbox;
}

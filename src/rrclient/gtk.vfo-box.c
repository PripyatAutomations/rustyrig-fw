//
// rrclient/gtk.vfo-box.c: VFO control widget
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

GtkWidget *tx_codec_combo = NULL, *rx_codec_combo = NULL;

static void on_conn_button_clicked(GtkButton *button, gpointer user_data) {
   connect_or_disconnect(GTK_BUTTON(button));
}

GtkWidget *create_vfo_box(void) {
   // Create the control box
   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

   // ONLINE button
   conn_button = gtk_button_new_with_label("Offline");
   gtk_widget_set_tooltip_text(conn_button, "Toggle online state");
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);

   GtkWidget *online_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), online_spacer, TRUE, TRUE, 0);

   // FREQ selection
   freq_entry = gtk_freq_entry_new(MAX_DIGITS);
   GtkWidget *freq_label = gtk_label_new("Hz");
   gtk_box_pack_start(GTK_BOX(control_box), freq_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), freq_label, FALSE, FALSE, 0);

   // MODE selection
   GtkWidget *mode_box = create_mode_box();
   gtk_box_pack_start(GTK_BOX(control_box), mode_box, TRUE, TRUE, 6);

   // CODEC selectors
   GtkWidget *codec_selectors = create_codec_selector_vbox(&tx_codec_combo, &rx_codec_combo);
   gtk_box_pack_start(GTK_BOX(control_box), codec_selectors, TRUE, TRUE, 6);

   // VOLUME selector
   GtkWidget *rx_vol_vbox = create_volbox();
   gtk_box_pack_start(GTK_BOX(control_box), rx_vol_vbox, TRUE, TRUE, 0);

   // TX POWER Box
   GtkWidget *tx_power_vbox = create_txpower_box();
   gtk_box_pack_start(GTK_BOX(control_box), tx_power_vbox, TRUE, TRUE, 6);

   // add a spacer next to PTT
   GtkWidget *ptt_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), ptt_spacer, TRUE, TRUE, 0);

   // Create PTT button widget
   GtkWidget *ptt_box = ptt_button_create();
   gtk_box_pack_start(GTK_BOX(control_box), ptt_box, TRUE, TRUE, 2);

   return control_box;
}

gui_window_t *create_vfo_window(GtkWidget *vfo_box, char vfo) {
   if (!vfo_box || vfo == '\0') {
      Log(LOG_CRIT, "gtk.vfo", "create_vfo_window invalid args: vfo_box <%x> vfo |%c|", vfo_box, vfo);
      return NULL;
   }

   // prepare a programmatic name for the VFO
   char win_name[32];
   memset(win_name, 0, sizeof(win_name));
   snprintf(win_name, sizeof(win_name), "vfo-%c", vfo);

   // Lets see if we recognize that window...
   gui_window_t *vfo_win = gui_find_window(NULL, win_name);
   if (vfo_win) {
     // Show the existing window
     place_window(vfo_win->gtk_win);
   } else {
      GtkWidget *vfo_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gui_window_t *vfo_win  = ui_new_window(vfo_window, win_name);
      gtk_window_set_title(GTK_WINDOW(vfo_window), "rustyrig remote client");
   }
   return vfo_win;
}

//
// rrclient/gtk/gtk.txpower.c: Transmit power widget
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/connman.h>
#include <rrclient/media.h>
#include <rrclient/vfo.h>

extern int rrclient_tx_power_percent;
/* Debounce scale changes so dragging never floods the control channel. */
static guint tx_power_send_id;
static gboolean tx_power_send(gpointer unused) {
   (void)unused;
   tx_power_send_id = 0;
   if (!tx_power_slider || !ws_conn || rrclient_connection_state(rrclient_selected_server()) != 1)
      return G_SOURCE_REMOVE;
   const char *room = rrclient_media_active_room();
   if (!room || !*room) {
      Log(LOG_WARN, "ui.gtk", "TX power: no active rig room for percentage request");
      return G_SOURCE_REMOVE;
   }
   if (rrclient_tx_power_percent == 0) return G_SOURCE_REMOVE;
   dict *d = dict_new();
   if (!d) return G_SOURCE_REMOVE;
   char vfo[] = { vfo_state_get_active(), '\0' };
   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.cmd", "power");
   dict_add(d, "cat.room", room);
   dict_add(d, "cat.vfo", vfo);
   dict_add_float(d, "cat.power", (float)gtk_range_get_value(GTK_RANGE(tx_power_slider)));
   dict_add(d, "cat.power-unit", "percent");
   if (!ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT))
      Log(LOG_WARN, "ui.gtk", "TX power: failed to send percent request for room %s VFO %s", room, vfo);
   dict_free(d);
   return G_SOURCE_REMOVE;
}
static void tx_power_changed(GtkRange *range, gpointer unused) {
   (void)range;
   (void)unused;
   rrclient_tx_power_percent = (int)gtk_range_get_value(range);
   if (tx_power_send_id) g_source_remove(tx_power_send_id);
   tx_power_send_id = g_timeout_add(200, tx_power_send, NULL);
}

GtkWidget *tx_power_slider = NULL;

GtkWidget *create_txpower_box(void) {
   GtkWidget *tx_power_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *tx_power_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(tx_power_label), "TX <u>P</u>ower");
   tx_power_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_widget_set_tooltip_text(tx_power_slider, "Transmit power as a percentage of the selected rig/mode maximum (0% inhibits client PTT ON (PTT OFF remains available))");
   gtk_widget_set_size_request(tx_power_slider, 120, -1);
   gtk_scale_set_draw_value(GTK_SCALE(tx_power_slider), TRUE);
   gtk_scale_set_value_pos(GTK_SCALE(tx_power_slider), GTK_POS_RIGHT);
   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_label, FALSE, FALSE, 1);
   gtk_box_pack_start(GTK_BOX(tx_power_vbox), tx_power_slider, FALSE, FALSE, 1);
   int cfg_def_pow_tx = cfg_get_int("default.tx.power", 0);
   gtk_range_set_value(GTK_RANGE(tx_power_slider), cfg_def_pow_tx >= 0 && cfg_def_pow_tx <= 100 ? cfg_def_pow_tx : 100);
   rrclient_tx_power_percent = (int)gtk_range_get_value(GTK_RANGE(tx_power_slider));
   g_signal_connect(tx_power_slider, "value-changed", G_CALLBACK(tx_power_changed), NULL);

   return tx_power_vbox;
}

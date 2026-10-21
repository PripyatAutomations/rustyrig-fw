//
// rrclient/gtk/gtk.vol-box.c: Transmit power/volume
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
#include <rrclient/audio.h>
#include <rrclient/objects.h>
#include <rrclient/media.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/vfo.h>
#include <rrclient/gtk/gtk.core.h>

GtkWidget *rx_vol_slider = NULL; // gstreamer volume
GtkWidget *rx_rig_vol_slider = NULL;   // Rig side setting

void on_rx_volume_changed(GtkRange *range, gpointer user_data) {
   (void)user_data;

   if (!range) {
      return;
   }
   audio_set_rx_volume((int)gtk_range_get_value(range));
}

/* Rig Vol is a property of the VFO's owning rig, not a client audio gain.
 * Keep the target transient: every update resolves the active room and VFO. */
static const char *rig_volume_owner(void) {
   const char *room = rrclient_active_room();
   if (!room || !*room) return NULL;
   const char *vfo = rrclient_media_vfo_uuid(room, vfo_state_get_active());
   const dict *descriptor = rrclient_object_descriptor(vfo);
   const char *owner = descriptor ? dict_get((dict *)descriptor, "object.owner", NULL) : NULL;
   const dict *rig = rrclient_object_descriptor(owner);
   return rig && !strcmp(dict_get((dict *)rig, "object.type", ""), "rig") ? owner : NULL;
}
static void on_rig_volume_changed(GtkRange *range, gpointer unused) {
   (void)unused;
   const char *owner = rig_volume_owner();
   rrconn_t *connection = rrclient_active_connection();
   if (!owner || !connection || !connection->authenticated || !connection->is_ws)
      return;
   dict *request = dict_new();
   if (!request) return;
   dict_add(request, "msg.type", "property");
   dict_add(request, "property.cmd", "set");
   dict_add(request, "target", owner);
   dict_add(request, "property.name", "audio.speaker.volume");
   dict_add_int(request, "property.value", (int)gtk_range_get_value(range));
   const char *room = rrclient_active_room();
   if (room && *room) dict_add(request, "request.room", room);
   ws_send_dict(NULL, connection, request, WEBSOCKET_OP_TEXT);
   dict_free(request);
}
static gboolean rig_volume_refresh(gpointer widget) {
   GtkWidget *slider = widget;
   if (!GTK_IS_RANGE(slider)) return G_SOURCE_REMOVE;
   const char *owner = rig_volume_owner();
   const dict *state = owner ? rrclient_object_property(owner, "audio.speaker.volume") : NULL;
   gboolean supported = state != NULL;
   gtk_widget_set_sensitive(slider, supported);
   if (supported && dict_get_bool((dict *)state, "property.known", false) &&
       !gtk_widget_has_focus(slider)) {
      int volume = dict_get_int((dict *)state, "property.value", 0);
      g_signal_handlers_block_by_func(slider, G_CALLBACK(on_rig_volume_changed), NULL);
      gtk_range_set_value(GTK_RANGE(slider), CLAMP(volume, 0, 100));
      g_signal_handlers_unblock_by_func(slider, G_CALLBACK(on_rig_volume_changed), NULL);
   }
   return G_SOURCE_CONTINUE;
}
// Returns a GtkWidget with the volume box
GtkWidget *create_volbox(void) {
   // RX VOLUME Box
   GtkWidget *rx_vol_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *rx_vol_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(rx_vol_label), "RX <u>V</u>ol");

   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_vol_slider), FALSE);
   gtk_widget_set_tooltip_text(rx_vol_slider, "Receive Volume");
   gtk_scale_set_digits(GTK_SCALE(rx_vol_slider), 0);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_label, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_vol_slider, TRUE, TRUE, 0);

   /// Put a spacer in here
   GtkWidget *vol_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), vol_spacer, TRUE, TRUE, 0);

   GtkWidget *rx_rig_vol_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(rx_rig_vol_label), "Ri<u>G</u> Vol");

   rx_rig_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_rig_vol_slider), FALSE);
   gtk_widget_set_tooltip_text(rx_rig_vol_slider, "Rig speaker Volume");
   gtk_scale_set_digits(GTK_SCALE(rx_rig_vol_slider), 0);
   gtk_widget_set_sensitive(rx_rig_vol_slider, FALSE);
   g_signal_connect(rx_rig_vol_slider, "value-changed", G_CALLBACK(on_rig_volume_changed), NULL);
   guint volume_refresh_source = g_timeout_add(500, rig_volume_refresh, rx_rig_vol_slider);
   g_signal_connect_swapped(rx_rig_vol_slider, "destroy", G_CALLBACK(g_source_remove), GUINT_TO_POINTER(volume_refresh_source));
   rig_volume_refresh(rx_rig_vol_slider);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_rig_vol_slider, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(rx_vol_vbox), rx_rig_vol_label, TRUE, TRUE, 0);

   int cfg_def_vol_rx = cfg_get_int("audio.volume.rx", 0);

   if (rx_vol_slider && GTK_IS_RANGE(rx_vol_slider)) {
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), cfg_def_vol_rx);
   }

   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(on_rx_volume_changed), NULL);

   return rx_vol_vbox;
}

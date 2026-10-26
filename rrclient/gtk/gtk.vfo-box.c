#include <rrclient/rooms.h>
//
// rrclient/gtk/gtk.vfo-box.c: VFO control widget
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
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/vfo.h>

extern GtkWidget *freq_entry;
extern int ws_connected;

typedef struct {
   GtkWidget *fe;               /* the GtkFreqEntry container */
   GtkWidget *chat_entry;
   GtkWidget *mode_combo;
} VfoKeyData;

GtkWidget *tx_codec_combo = NULL, *rx_codec_combo = NULL;
static GtkWidget *vfo_tunable_box = NULL;
static GtkWidget *vfo_layout_box = NULL;
static GtkWidget *vfo_controls_row = NULL;
static GtkWidget *vfo_mode_box = NULL;
static GtkWidget *vfo_codec_box = NULL;
static GtkWidget *vfo_ptt_box = NULL, *vfo_tx_power_box = NULL;

/* Preserve RX frequency, mode and audio while removing TX affordances from receive-only rooms and remotely sliced SDR VFOs. */
void vfo_controls_set_rx_only(bool rx_only) {
   if (vfo_ptt_box) {
      gtk_widget_set_visible(vfo_ptt_box, !rx_only);
   }

   if (vfo_tx_power_box) {
      gtk_widget_set_visible(vfo_tx_power_box, !rx_only);
   }

   if (tx_codec_combo) {
      gtk_widget_set_visible(tx_codec_combo, !rx_only);
   }
}

// Should we pause the VFO widget being updated until after the server has sent response?
bool vfo_muted = false;
struct timeval vfo_muted_since = {
   0
};

void vfo_controls_set_ptt_state(bool any_tx, bool own_tx) {
   bool offline = rrclient_room_offline(rrclient_current_room());

   if (vfo_tunable_box) {
      gtk_widget_set_sensitive(vfo_tunable_box, !any_tx && !offline);
   }

   if (vfo_mode_box) {
      gtk_widget_set_sensitive(vfo_mode_box, !any_tx && !offline);
   }

   if (vfo_tx_power_box) {
      gtk_widget_set_sensitive(vfo_tx_power_box, !any_tx && !offline);
   }

   if (vfo_codec_box) {
      // RX codec selection remains available while another user transmits.
      // TX is locked separately below because it is shared by everyone on
      // the transmitting channel.
      gtk_widget_set_sensitive(vfo_codec_box, !offline);
   }

   if (tx_codec_combo) {
      // Changing this codec changes the shared TX stream and can leave an
      // active listener unable to decode it.
      gtk_widget_set_sensitive(tx_codec_combo, !any_tx && !offline);
   }
}

static gboolean on_vfo_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
   if (!widget || !event || !user_data) {
      return FALSE;
   }
   VfoKeyData *d = user_data;

   if (!d || !GTK_IS_WIDGET(d->fe)) {
      return FALSE;
   }

   if (!is_widget_or_descendant_focused(d->fe)) {
      return FALSE;             /* ignore keys unless focus is somewhere inside fe */
   }

   if ((event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab) && (event->state & GDK_SHIFT_MASK)) {
      gtk_widget_grab_focus(d->chat_entry);

      return TRUE;
   } else if (event->keyval == GDK_KEY_Tab && !(event->state & GDK_SHIFT_MASK)) {
      gtk_widget_grab_focus(d->mode_combo);

      return TRUE;
   }

   return FALSE;                /* let other keys be handled normally */
}

void vfo_controls_fit_width(int width) {
   if (width < 1) {
      return;
   }

   /* Frequency always has its own row; only the controls below it reflow. */
   if (GTK_IS_ORIENTABLE(vfo_controls_row)) {
      gtk_orientable_set_orientation(GTK_ORIENTABLE(vfo_controls_row), width < 600 ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
   }
}

GtkWidget *create_vfo_box(void) {
   // Create the control box
   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   vfo_layout_box = control_box;
   g_object_add_weak_pointer(G_OBJECT(control_box), (gpointer *) &vfo_layout_box);

   vfo_tunable_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), vfo_tunable_box, FALSE, FALSE, 0);
   vfo_controls_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
   gtk_box_pack_start(GTK_BOX(control_box), vfo_controls_row, FALSE, FALSE, 0);

   // FREQ selection
   freq_entry = gtk_freq_entry_new(-1);
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), freq_entry, TRUE, TRUE, 0);

   // MODE selection
   GtkWidget *mode_box = create_mode_box();
   vfo_mode_box = mode_box;
   gtk_box_pack_start(GTK_BOX(vfo_controls_row), mode_box, FALSE, FALSE, 0);

   // CODEC selectors
   GtkWidget *codec_selectors = create_codec_selector_vbox(&tx_codec_combo, &rx_codec_combo);
   vfo_codec_box = codec_selectors;
   gtk_box_pack_start(GTK_BOX(vfo_controls_row), codec_selectors, FALSE, FALSE, 0);

   // TX POWER Box
   GtkWidget *tx_power_vbox = create_txpower_box();
   vfo_tx_power_box = tx_power_vbox;
   g_object_add_weak_pointer(G_OBJECT(tx_power_vbox), (gpointer *) &vfo_tx_power_box);
   gtk_box_pack_start(GTK_BOX(vfo_controls_row), tx_power_vbox, FALSE, FALSE, 0);

   // These globals are borrowed widget references used by protocol/event
   // callbacks.  Clear them automatically if the VFO window is destroyed.
   if (vfo_tunable_box) {
      g_object_add_weak_pointer(G_OBJECT(vfo_tunable_box), (gpointer *) &vfo_tunable_box);
   }

   g_object_add_weak_pointer(G_OBJECT(vfo_controls_row), (gpointer *) &vfo_controls_row);
   g_object_add_weak_pointer(G_OBJECT(vfo_mode_box), (gpointer *) &vfo_mode_box);

   if (vfo_codec_box) {
      g_object_add_weak_pointer(G_OBJECT(vfo_codec_box), (gpointer *) &vfo_codec_box);
   }

   if (freq_entry) {
      g_object_add_weak_pointer(G_OBJECT(freq_entry), (gpointer *) &freq_entry);
   }

   if (mode_combo) {
      g_object_add_weak_pointer(G_OBJECT(mode_combo), (gpointer *) &mode_combo);
   }

   if (width_combo) {
      g_object_add_weak_pointer(G_OBJECT(width_combo), (gpointer *) &width_combo);
   }

   if (tx_codec_combo) {
      g_object_add_weak_pointer(G_OBJECT(tx_codec_combo), (gpointer *) &tx_codec_combo);
   }

   if (rx_codec_combo) {
      g_object_add_weak_pointer(G_OBJECT(rx_codec_combo), (gpointer *) &rx_codec_combo);
   }

   if (rx_vol_slider) {
      g_object_add_weak_pointer(G_OBJECT(rx_vol_slider), (gpointer *) &rx_vol_slider);
   }

   if (rx_rig_vol_slider) {
      g_object_add_weak_pointer(G_OBJECT(rx_rig_vol_slider), (gpointer *) &rx_rig_vol_slider);
   }
   // Create PTT button widget; pack at the far right edge. NB: must be
   // packed FALSE/FALSE - with expand=TRUE the box stretches but the button
   // inside sits at its left, leaving it shy of the window edge.
   GtkWidget *ptt_box = ptt_button_create();
   vfo_ptt_box = ptt_box;
   g_object_add_weak_pointer(G_OBJECT(ptt_box), (gpointer *) &vfo_ptt_box);
   gtk_box_pack_end(GTK_BOX(vfo_controls_row), ptt_box, FALSE, FALSE, 0);

   // This will sort out tab order between previous/next widget
   g_signal_connect(control_box, "key-press-event", G_CALLBACK(on_vfo_key_press), freq_entry);

   /* CAT state commonly arrives while authentication is building the room tab.  The user list reads that cached state directly, but the controls are created
    * afterwards and would otherwise keep their zero/default values until the next CAT poll.  Push the already-received state into the newly-created widgets
    * immediately. */
   vfo_update_ui();

   return control_box;
}

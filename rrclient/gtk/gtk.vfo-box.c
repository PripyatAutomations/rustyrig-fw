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
static GtkWidget *vfo_codec_box = NULL;
static GtkWidget *vfo_ptt_box = NULL, *vfo_tx_power_box = NULL;

/* Binary VFO drawer: a GTK divider-style grip, never a partly exposed panel. The grab distance is measured in root coordinates so it works in docked and
 * detached windows alike.  The grip remains visible when collapsed. */
typedef struct {
   GtkWidget *revealer;
   double press_y, press_x;
   bool pressed;
} VfoDrawer;

static void vfo_drawer_free(gpointer data) {
   g_free(data);
}

static gboolean vfo_drawer_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   VfoDrawer *drawer = data;

   if (event->button != GDK_BUTTON_PRIMARY) {
      return FALSE;
   }
   drawer->press_y = event->y_root;
   drawer->press_x = event->x_root;
   drawer->pressed = true;

   return TRUE;
}

/* A horizontal drag tears off the panel; vertical swipes remain collapse gestures. */
extern void gtk_chat_vfo_handle_drag(void);
static gboolean vfo_drawer_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
   (void)widget;
   VfoDrawer *drawer = data;

   if (!drawer->pressed) {
      return FALSE;
   }
   double dx = event->x_root - drawer->press_x;

   if (dx > 48.0 || dx < -48.0) {
      drawer->pressed = false;
      gtk_chat_vfo_handle_drag();

      return TRUE;
   }

   return FALSE;
}
static gboolean vfo_drawer_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
   VfoDrawer *drawer = data;

   if (event->button != GDK_BUTTON_PRIMARY || !drawer->pressed) {
      return FALSE;
   }
   drawer->pressed = false;
   gboolean open = gtk_revealer_get_reveal_child(GTK_REVEALER(drawer->revealer));
   double delta = event->y_root - drawer->press_y;

   if (delta >= 24.0) {
      open = true;
   } else if (delta <= -24.0) {
      open = false;
   } else if (delta > -6.0 && delta < 6.0) {
      open = !open;             /* click */
   }
   gtk_revealer_set_reveal_child(GTK_REVEALER(drawer->revealer), open);
   gtk_widget_set_tooltip_text(widget, open ? "Drag up or click to close VFO controls" : "Drag down or click to open VFO controls");

   return TRUE;
}

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

static void on_conn_button_clicked(GtkButton *button, gpointer user_data) {
   if (!button) {
      return;
   }
   connect_or_disconnect(server_name);
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

GtkWidget *create_vfo_box(void) {
   // The grip stays visible; only the controls are revealed or hidden.
   GtkWidget *drawer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *revealer = gtk_revealer_new();
   gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
   gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 140);
   gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
   GtkWidget *grip = gtk_event_box_new();
   g_object_set_data(G_OBJECT(drawer_box), "rr-vfo-grip", grip);
   g_object_set_data(G_OBJECT(drawer_box), "rr-vfo-revealer", revealer);
   gtk_widget_add_events(grip, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_TOUCH_MASK);
   gtk_widget_set_tooltip_text(grip, "Click to toggle; swipe up/down to fold; drag sideways to float; right-click to dock top/bottom");
   GtkWidget *grip_label = gtk_label_new("  ▰▰▰  ");
   gtk_widget_set_margin_top(grip_label, 3);
   gtk_widget_set_margin_bottom(grip_label, 3);
   gtk_widget_set_halign(grip_label, GTK_ALIGN_CENTER);
   gtk_container_add(GTK_CONTAINER(grip), grip_label);
   gtk_box_pack_start(GTK_BOX(drawer_box), grip, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(drawer_box), revealer, FALSE, FALSE, 0);
   VfoDrawer *drawer = g_new0(VfoDrawer, 1);
   drawer->revealer = revealer;
   g_object_set_data_full(G_OBJECT(grip), "vfo-drawer", drawer, vfo_drawer_free);
   g_signal_connect(grip, "button-press-event", G_CALLBACK(vfo_drawer_press), drawer);
   g_signal_connect(grip, "button-release-event", G_CALLBACK(vfo_drawer_release), drawer);
   g_signal_connect(grip, "motion-notify-event", G_CALLBACK(vfo_drawer_motion), drawer);
   // Create the control box
   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

   gtk_container_add(GTK_CONTAINER(revealer), control_box);

   // ONLINE button
   conn_button = gtk_button_new_with_label("_Offline");
   gtk_widget_set_tooltip_text(conn_button, "Toggle online state");
   // fixed min width so label changes (Online/Offline/Trying...) don't resize it
   gtk_widget_set_size_request(conn_button, 110, -1);
   gtk_box_pack_start(GTK_BOX(control_box), conn_button, FALSE, FALSE, 0);
   GtkStyleContext *conn_ctx = gtk_widget_get_style_context(conn_button);
   gtk_style_context_add_class(conn_ctx, "conn-idle");
   /* The connection may already be established when a VFO widget is built (for example after the authoritative room arrives).  Paint the button from the cached
    * protocol state immediately instead of leaving the initial _Offline label until the next click. */
   update_connection_button(ws_connected, conn_button);
   g_signal_connect(conn_button, "clicked", G_CALLBACK(on_conn_button_clicked), NULL);


   vfo_tunable_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(control_box), vfo_tunable_box, TRUE, TRUE, 0);

   // FREQ selection
   freq_entry = gtk_freq_entry_new(-1);
   GtkWidget *freq_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(freq_label), "<u>F</u>req");
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), freq_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), freq_label, FALSE, FALSE, 0);

   // MODE selection
   GtkWidget *mode_box = create_mode_box();
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), mode_box, TRUE, TRUE, 6);

   // CODEC selectors
   GtkWidget *codec_selectors = create_codec_selector_vbox(&tx_codec_combo, &rx_codec_combo);
   vfo_codec_box = codec_selectors;
   gtk_box_pack_start(GTK_BOX(control_box), codec_selectors, TRUE, TRUE, 6);

   // VOLUME selector
   GtkWidget *rx_vol_vbox = create_volbox();
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), rx_vol_vbox, TRUE, TRUE, 0);

   // TX POWER Box
   GtkWidget *tx_power_vbox = create_txpower_box();
   vfo_tx_power_box = tx_power_vbox;
   g_object_add_weak_pointer(G_OBJECT(tx_power_vbox), (gpointer *) &vfo_tx_power_box);
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), tx_power_vbox, TRUE, TRUE, 6);

   // These globals are borrowed widget references used by protocol/event
   // callbacks.  Clear them automatically if the VFO window is destroyed.
   if (conn_button) {
      g_object_add_weak_pointer(G_OBJECT(conn_button), (gpointer *) &conn_button);
   }

   if (vfo_tunable_box) {
      g_object_add_weak_pointer(G_OBJECT(vfo_tunable_box), (gpointer *) &vfo_tunable_box);
   }

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
   gtk_box_pack_end(GTK_BOX(control_box), ptt_box, FALSE, FALSE, 2);

   // This will sort out tab order between previous/next widget
   g_signal_connect(control_box, "key-press-event", G_CALLBACK(on_vfo_key_press), freq_entry);

   /* CAT state commonly arrives while authentication is building the room tab.  The user list reads that cached state directly, but the controls are created
    * afterwards and would otherwise keep their zero/default values until the next CAT poll.  Push the already-received state into the newly-created widgets
    * immediately. */
   vfo_update_ui();

   return drawer_box;
}

gui_window_t *create_vfo_window(GtkWidget *vfo_box, char vfo) {
   if (!vfo_box || vfo == '\0') {
      Log(LOG_CRIT, "gtk.vfo", "create_vfo_window invalid args: vfo_box <%p> vfo |%c|", vfo_box, vfo);

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
      gui_window_t *vfo_win = ui_new_window(vfo_window, win_name);
      gtk_window_set_title(GTK_WINDOW(vfo_window), "rrclient vfo");
   }

   return vfo_win;
}

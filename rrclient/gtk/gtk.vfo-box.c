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
#include <rrclient/database.h>
#include <rrclient/local.bridge.h>
#include <librustyaxe/event-bus.h>

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
static GtkWidget *vfo_sdr_button;
static bool vfo_local_sdr, vfo_rx_only, vfo_sdr_connected;
static char vfo_sdr_driver[32];
static rr_event_token_t local_sdr_driver_token;

static void vfo_sdr_button_update(void) {
   if (!GTK_IS_BUTTON(vfo_sdr_button)) {
      return;
   }
   const char *label = !g_ascii_strcasecmp(vfo_sdr_driver, "kiwi") ? "KiwiSDR" :
      !g_ascii_strcasecmp(vfo_sdr_driver, "rtl_tcp") ? "RTL-SDR" : !g_ascii_strcasecmp(vfo_sdr_driver, "sdrplay") ? "SDRplay" : NULL;
   char *generic = NULL;

   if (!label && vfo_sdr_driver[0]) {
      generic = g_strdup(vfo_sdr_driver);

      for (char *p = generic ; *p ; p++) {
         if (*p == '_' || *p == '-') {
            *p = ' ';
         }
      }

      generic[0] = g_ascii_toupper(generic[0]);
      label = generic;
   }
   gtk_button_set_label(GTK_BUTTON(vfo_sdr_button), label ? label : "Remote SDR");
   char *tooltip = g_strdup_printf("Open %s controls", label ? label : "remote SDR");
   gtk_widget_set_tooltip_text(vfo_sdr_button, tooltip);
   gtk_widget_set_visible(vfo_sdr_button, vfo_local_sdr && vfo_sdr_connected);
   g_free(tooltip);
   g_free(generic);
}

/* Preserve RX frequency, mode and audio while removing TX affordances from receive-only rooms and remotely sliced SDR VFOs. */
void vfo_controls_set_rx_only(bool rx_only) {
   vfo_rx_only = rx_only;

   if (vfo_ptt_box) {
      gtk_widget_set_visible(vfo_ptt_box, !rx_only);
   }

   if (vfo_tx_power_box) {
      gtk_widget_set_visible(vfo_tx_power_box, !rx_only && !vfo_local_sdr);
   }

   if (tx_codec_combo) {
      gtk_widget_set_visible(tx_codec_combo, !rx_only);
   }

   if (vfo_codec_box) {
      const char *room = gtk_chat_controls_room();
      gtk_widget_set_visible(vfo_codec_box, !room || g_ascii_strcasecmp(room, "&local-sdr") != 0);
   }
}

void vfo_controls_set_local_sdr(bool local) {
   vfo_local_sdr = local;

   if (vfo_codec_box) {
      gtk_widget_set_visible(vfo_codec_box, !local);
   }

   if (vfo_tx_power_box) {
      gtk_widget_set_visible(vfo_tx_power_box, !local && !vfo_rx_only);
   }
   vfo_sdr_button_update();

   if (local) {
      rr_event_message_t request = {
         .name = RRCLIENT_LOCAL_BRIDGE_ACTIVE_REQUEST
      };
      event_emit_message(&request);
   }
}

void vfo_controls_set_sdr_driver(const rrclient_local_bridge_active_t *active) {
   if (!active || !memchr(active->driver, 0, sizeof(active->driver))) {
      return;
   }
   g_strlcpy(vfo_sdr_driver, active->driver, sizeof(vfo_sdr_driver));
   vfo_sdr_connected = active->connected;
   vfo_sdr_button_update();
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

   /* The FlowBox owns responsive wrapping and user-defined ordering.
    * Changing its orientation at a threshold destroys that layout. */
   if (GTK_IS_FLOW_BOX(vfo_controls_row))
      gtk_widget_queue_resize(vfo_controls_row);
}

static rr_event_token_t local_vfo_token;
static void local_sdr_driver_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message->payload || message->length != sizeof(rrclient_local_bridge_active_t)) {
      return;
   }
   vfo_controls_set_sdr_driver(message->payload);
}
static void local_sdr_controls_clicked(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   gtk_local_sdr_controls_show();
}

static void local_vfo_refresh(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;
   const char *room = gtk_chat_controls_room();

   if (!freq_entry || !room || g_ascii_strcasecmp(room, "&local-sdr")) {
      return;
   }
   rr_local_bridge_ui_state_t state = {0};
   if (!rrclient_local_bridge_ui_state(&state) || !state.frequency_hz)
      return;
   uint64_t hz = state.frequency_hz;
   GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);

   if (!gtk_freq_entry_is_editing(fe)) {
      gtk_freq_entry_set_frequency(fe, (unsigned long)hz);
   }
   modebox_update_state(state.mode, 0);
   vfo_controls_set_rx_only(true);

   if (vfo_tunable_box) {
      gtk_widget_set_sensitive(vfo_tunable_box, TRUE);
   }

   if (vfo_mode_box) {
      gtk_widget_set_sensitive(vfo_mode_box, TRUE);
   }
}
static void local_vfo_destroy(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;

   if (local_vfo_token) {
      event_off_token(local_vfo_token);
   }
   local_vfo_token = 0;

   if (local_sdr_driver_token) {
      event_off_token(local_sdr_driver_token);
   }
   local_sdr_driver_token = 0;
}
/* A FlowBox supplies the gravity/reflow; DnD changes the packing order.
 * Only grab handles are drag sources, so CAT digits and PTT remain clickable. */
static GtkWidget *vfo_dragging_child;
/* Store order, not coordinates: GtkFlowBox keeps docks packed and wrapping
 * correctly even after panel resize and zoom changes. */
static void vfo_layout_save(void) {
   if (!GTK_IS_FLOW_BOX(vfo_controls_row)) return;
   GString *order = g_string_new(NULL);
   GList *children = gtk_container_get_children(GTK_CONTAINER(vfo_controls_row));
   for (GList *it = children; it; it = it->next) {
      const char *id = g_object_get_data(G_OBJECT(it->data), "rr-vfo-id");
      if (id) {
         if (order->len) g_string_append_c(order, ',');
         g_string_append(order, id);
      }
   }
   rrclient_database_vfo_layout_save(order->str);
   g_list_free(children);
   g_string_free(order, TRUE);
}
static void vfo_layout_restore(void) {
   if (!GTK_IS_FLOW_BOX(vfo_controls_row)) return;
   char *stored = rrclient_database_vfo_layout_load();
   if (!stored) return;
   gchar **ids = g_strsplit(stored, ",", -1);
   guint index = 0;
   for (guint i = 0; ids[i]; i++) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(vfo_controls_row));
      GtkWidget *found = NULL;
      for (GList *it = children; it; it = it->next) {
         if (!g_strcmp0(g_object_get_data(G_OBJECT(it->data), "rr-vfo-id"), ids[i])) {
            found = it->data; break;
         }
      }
      if (found) {
         g_object_ref(found);
         gtk_container_remove(GTK_CONTAINER(vfo_controls_row), found);
         gtk_flow_box_insert(GTK_FLOW_BOX(vfo_controls_row), found, index++);
         gtk_widget_show(found);
         g_object_unref(found);
      }
      g_list_free(children);
   }
   g_strfreev(ids);
   g_free(stored);
}

static gchar vfo_drag_target_name[] = "application/x-rustyrig-vfo-control";
static GtkTargetEntry vfo_drag_target[] = {
   { vfo_drag_target_name, GTK_TARGET_SAME_APP, 0 }
};
static GtkCssProvider *vfo_dock_css;

static void vfo_dock_style(void) {
   if (vfo_dock_css) return;
   vfo_dock_css = gtk_css_provider_new();
   gtk_css_provider_load_from_data(vfo_dock_css,
      ".rr-vfo-group { border: 2px solid transparent; border-radius: 4px; }"
      ".rr-vfo-group.rr-vfo-moving { border-color: #66caff; background-color: rgba(80,180,255,.17); }"
      ".rr-vfo-group.rr-vfo-before { border-left: 4px solid #48c5ff; }"
      ".rr-vfo-group.rr-vfo-after { border-right: 4px solid #48c5ff; }"
      ".rr-vfo-group.rr-vfo-shift { background-color: rgba(60,180,240,.10); box-shadow: inset 0 0 0 1px #479bd0; }"
      ".rr-vfo-grip { color: #8bbbd1; font-size: 10px; padding: 0 4px; }", -1, NULL);
}
static void vfo_clear_drop(GtkWidget *child) {
   if (!child) return;
   GtkStyleContext *ctx = gtk_widget_get_style_context(child);
   gtk_style_context_remove_class(ctx, "rr-vfo-before");
   gtk_style_context_remove_class(ctx, "rr-vfo-after");
   gtk_style_context_remove_class(ctx, "rr-vfo-shift");
}
static void vfo_drag_begin(GtkWidget *handle, GdkDragContext *context, gpointer unused) {
   (void)context; (void)unused;
   GtkWidget *child = g_object_get_data(G_OBJECT(handle), "rr-vfo-child");
   vfo_dragging_child = child;
   if (child)
      gtk_style_context_add_class(gtk_widget_get_style_context(child), "rr-vfo-moving");
}
static void vfo_drag_end(GtkWidget *handle, GdkDragContext *context, gpointer unused) {
   (void)handle; (void)context; (void)unused;
   if (vfo_dragging_child)
      gtk_style_context_remove_class(gtk_widget_get_style_context(vfo_dragging_child), "rr-vfo-moving");
   vfo_dragging_child = NULL;
   if (!GTK_IS_CONTAINER(vfo_controls_row)) return;
   GList *children = gtk_container_get_children(GTK_CONTAINER(vfo_controls_row));
   for (GList *it = children; it; it = it->next)
      vfo_clear_drop(it->data);
   g_list_free(children);
}
static void vfo_drag_data_get(GtkWidget *widget, GdkDragContext *context,
                               GtkSelectionData *selection, guint info, guint time,
                               gpointer unused) {
   (void)widget; (void)context; (void)info; (void)time; (void)unused;
   const char value[] = "vfo";
   gtk_selection_data_set(selection, gdk_atom_intern_static_string(vfo_drag_target[0].target),
                          8, (const guchar *)value, sizeof(value) - 1);
}
static void vfo_preview_shift(GtkWidget *target, gboolean after) {
   if (!GTK_IS_CONTAINER(vfo_controls_row)) return;
   GList *children = gtk_container_get_children(GTK_CONTAINER(vfo_controls_row));
   gint from = g_list_index(children, vfo_dragging_child);
   gint to = g_list_index(children, target);
   if (after) to++;
   if (from >= 0 && from < to) to--;
   gint index = 0;
   for (GList *it = children; it; it = it->next, index++) {
      GtkWidget *child = it->data;
      GtkStyleContext *ctx = gtk_widget_get_style_context(child);
      gboolean shifts = child != vfo_dragging_child &&
         ((from < to && index > from && index <= to) ||
          (from > to && index >= to && index < from));
      if (shifts) gtk_style_context_add_class(ctx, "rr-vfo-shift");
      else gtk_style_context_remove_class(ctx, "rr-vfo-shift");
   }
   g_list_free(children);
}
static gboolean vfo_drop_after(GtkWidget *child, gint x) {
   return x >= gtk_widget_get_allocated_width(child) / 2;
}
static gboolean vfo_drag_motion(GtkWidget *child, GdkDragContext *context,
                                 gint x, gint y, guint time, gpointer unused) {
   (void)y; (void)unused;
   if (!vfo_dragging_child || vfo_dragging_child == child) {
      vfo_clear_drop(child);
      return FALSE;
   }
   gboolean after = vfo_drop_after(child, x);
   GtkStyleContext *ctx = gtk_widget_get_style_context(child);
   gtk_style_context_remove_class(ctx, after ? "rr-vfo-before" : "rr-vfo-after");
   gtk_style_context_add_class(ctx, after ? "rr-vfo-after" : "rr-vfo-before");
   vfo_preview_shift(child, after);
   gdk_drag_status(context, GDK_ACTION_MOVE, time);
   return TRUE;
}
static void vfo_drag_leave(GtkWidget *child, GdkDragContext *context,
                            guint time, gpointer unused) {
   (void)context; (void)time; (void)unused;
   vfo_clear_drop(child);
}
static void vfo_drag_received(GtkWidget *child, GdkDragContext *context,
                               gint x, gint y, GtkSelectionData *selection,
                               guint info, guint time, gpointer unused) {
   (void)y; (void)info; (void)unused;
   gboolean ok = FALSE;
   GtkWidget *source = vfo_dragging_child;
   if (source && source != child && GTK_IS_FLOW_BOX(vfo_controls_row) &&
       gtk_selection_data_get_length(selection) == 3 &&
       memcmp(gtk_selection_data_get_data(selection), "vfo", 3) == 0) {
      GList *siblings = gtk_container_get_children(GTK_CONTAINER(vfo_controls_row));
      gint source_pos = g_list_index(siblings, source);
      gint target_pos = g_list_index(siblings, child);
      gint to = target_pos + (vfo_drop_after(child, x) ? 1 : 0);
      if (source_pos >= 0 && target_pos >= 0) {
         if (source_pos < to) to--;
         if (source_pos != to) {
            g_object_ref(source);
            gtk_container_remove(GTK_CONTAINER(vfo_controls_row), source);
            gtk_flow_box_insert(GTK_FLOW_BOX(vfo_controls_row), source, to);
            g_object_unref(source);
            gtk_widget_show(source);
         }
         ok = TRUE;
         vfo_layout_save();
      }
      g_list_free(siblings);
   }
   vfo_clear_drop(child);
   gtk_drag_finish(context, ok, ok, time);
}
static gboolean vfo_control_visible(GtkFlowBoxChild *child, gpointer unused) {
   (void)unused;
   GtkWidget *control = g_object_get_data(G_OBJECT(child), "rr-vfo-content");
   return control && gtk_widget_get_visible(control);
}
static void vfo_control_visibility_changed(GtkWidget *widget, GParamSpec *spec, gpointer user) {
   (void)widget; (void)spec;
   if (GTK_IS_FLOW_BOX(user))
      gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(user));
}
static void vfo_control_add(GtkWidget *control, const char *name) {
   gboolean wanted = gtk_widget_get_visible(control);
   gtk_widget_show_all(control);
   gtk_widget_set_no_show_all(control, TRUE);
   gtk_widget_set_visible(control, wanted);
   GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *handle = gtk_event_box_new();
   GtkWidget *hint = gtk_label_new(name);
   gtk_label_set_xalign(GTK_LABEL(hint), 0.5);
   gtk_widget_set_name(hint, "rr-vfo-drag-label");
   gtk_widget_set_tooltip_text(handle, "Drag to rearrange VFO controls");
   gtk_container_add(GTK_CONTAINER(handle), hint);
   gtk_box_pack_start(GTK_BOX(group), handle, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(group), control, FALSE, FALSE, 0);
   gtk_container_add(GTK_CONTAINER(vfo_controls_row), group);
   GtkWidget *child = gtk_widget_get_parent(group);
   g_object_set_data(G_OBJECT(child), "rr-vfo-content", control);
   const char *id = !strcmp(name, "⋮⋮ Frequency") ? "frequency" :
                    !strcmp(name, "⋮⋮ Mode") ? "mode" :
                    !strcmp(name, "⋮⋮ Codecs") ? "codecs" :
                    !strcmp(name, "⋮⋮ TX Power") ? "txpower" :
                    !strcmp(name, "⋮⋮ SDR") ? "sdr" : "ptt";
   g_object_set_data(G_OBJECT(child), "rr-vfo-id", (gpointer)id);
   g_object_set_data(G_OBJECT(handle), "rr-vfo-child", child);
   gtk_style_context_add_class(gtk_widget_get_style_context(child), "rr-vfo-group");
   gtk_style_context_add_class(gtk_widget_get_style_context(hint), "rr-vfo-grip");
   vfo_dock_style();
   gtk_style_context_add_provider(gtk_widget_get_style_context(child),
      GTK_STYLE_PROVIDER(vfo_dock_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   gtk_style_context_add_provider(gtk_widget_get_style_context(hint),
      GTK_STYLE_PROVIDER(vfo_dock_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   gtk_drag_source_set(handle, GDK_BUTTON1_MASK, vfo_drag_target,
                       G_N_ELEMENTS(vfo_drag_target), GDK_ACTION_MOVE);
   gtk_drag_dest_set(child, GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
                     vfo_drag_target, G_N_ELEMENTS(vfo_drag_target), GDK_ACTION_MOVE);
   g_signal_connect(handle, "drag-begin", G_CALLBACK(vfo_drag_begin), NULL);
   g_signal_connect(handle, "drag-end", G_CALLBACK(vfo_drag_end), NULL);
   g_signal_connect(handle, "drag-data-get", G_CALLBACK(vfo_drag_data_get), NULL);
   g_signal_connect(child, "drag-motion", G_CALLBACK(vfo_drag_motion), NULL);
   g_signal_connect(child, "drag-leave", G_CALLBACK(vfo_drag_leave), NULL);
   g_signal_connect(child, "drag-data-received", G_CALLBACK(vfo_drag_received), NULL);
   g_signal_connect_object(control, "notify::visible",
      G_CALLBACK(vfo_control_visibility_changed), G_OBJECT(vfo_controls_row), 0);
   /* The FlowBox may filter the new child before rr-vfo-content exists. */
   gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(vfo_controls_row));
   gtk_widget_show_all(group);
   /* Revealing the wrapper must never force an RX-only control visible. */
   gtk_widget_set_visible(control, wanted);
}
GtkWidget *create_vfo_box(void) {
   // Create the control box
   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   vfo_layout_box = control_box;
   g_object_add_weak_pointer(G_OBJECT(control_box), (gpointer *) &vfo_layout_box);

   vfo_tunable_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   vfo_controls_row = gtk_flow_box_new();
   gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(vfo_controls_row), GTK_SELECTION_NONE);
   gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(vfo_controls_row), 1);
   gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(vfo_controls_row), 4);
   gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(vfo_controls_row), 4);
   gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(vfo_controls_row), 4);
   gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(vfo_controls_row), FALSE);
   gtk_flow_box_set_filter_func(GTK_FLOW_BOX(vfo_controls_row), vfo_control_visible, NULL, NULL);
   gtk_widget_set_halign(vfo_controls_row, GTK_ALIGN_FILL);
   gtk_box_pack_start(GTK_BOX(control_box), vfo_controls_row, FALSE, FALSE, 0);

   // FREQ selection
   freq_entry = gtk_freq_entry_new(-1);
   gtk_box_pack_start(GTK_BOX(vfo_tunable_box), freq_entry, TRUE, TRUE, 0);
   gtk_widget_show(vfo_tunable_box);
   vfo_control_add(vfo_tunable_box, "⋮⋮ Frequency");

   // MODE selection
   GtkWidget *mode_box = create_mode_box();
   vfo_mode_box = mode_box;
   gtk_widget_show(mode_box);
   vfo_control_add(mode_box, "⋮⋮ Mode");

   // CODEC selectors
   GtkWidget *codec_selectors = create_codec_selector_vbox(&tx_codec_combo, &rx_codec_combo);
   vfo_codec_box = codec_selectors;
   {
      const char *room = gtk_chat_controls_room();
      gtk_widget_set_visible(codec_selectors, !room || g_ascii_strcasecmp(room, "&local-sdr") != 0);
   }
   vfo_control_add(codec_selectors, "⋮⋮ Codecs");

   // TX power for transceivers; the same space hosts the active remote SDR button.
   GtkWidget *tx_power_vbox = create_txpower_box();
   vfo_tx_power_box = tx_power_vbox;
   g_object_add_weak_pointer(G_OBJECT(tx_power_vbox), (gpointer *) &vfo_tx_power_box);
   gtk_widget_show(tx_power_vbox);
   vfo_control_add(tx_power_vbox, "⋮⋮ TX Power");
   vfo_sdr_button = gtk_button_new_with_label("Remote SDR");
   gtk_widget_set_no_show_all(vfo_sdr_button, TRUE);
   gtk_widget_set_tooltip_text(vfo_sdr_button, "Remote SDR controls");
   g_signal_connect(vfo_sdr_button, "clicked", G_CALLBACK(local_sdr_controls_clicked), NULL);
   vfo_control_add(vfo_sdr_button, "⋮⋮ SDR");
   g_object_add_weak_pointer(G_OBJECT(vfo_sdr_button), (gpointer *) &vfo_sdr_button);

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
   gtk_widget_show(ptt_box);
   vfo_control_add(ptt_box, "⋮⋮ PTT");
   vfo_layout_restore();

   // This will sort out tab order between previous/next widget
   g_signal_connect(control_box, "key-press-event", G_CALLBACK(on_vfo_key_press), freq_entry);

   /* CAT state commonly arrives while authentication is building the room tab.  The user list reads that cached state directly, but the controls are created
    * afterwards and would otherwise keep their zero/default values until the next CAT poll.  Push the already-received state into the newly-created widgets
    * immediately. */
   vfo_update_ui();

   if (!local_vfo_token) {
      local_vfo_token = event_on_domain_token("client.local-bridge.vfo", local_vfo_refresh, NULL, "gtk");
   }
   local_sdr_driver_token = event_on_message_domain_token(RRCLIENT_LOCAL_BRIDGE_ACTIVE_EVENT, local_sdr_driver_event, NULL, "gtk");
   g_signal_connect(control_box, "destroy", G_CALLBACK(local_vfo_destroy), NULL);
   local_vfo_refresh(NULL, NULL, NULL, NULL);

   return control_box;
}

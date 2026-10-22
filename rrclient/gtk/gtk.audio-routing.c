// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include "gtk.audio-routing.h"
#include "gtk.floating-panel.h"
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_combo[2], *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
static guint routing_source;
static rr_event_token_t streams_token, routes_token;
static bool routing_updating, inventory_dirty, have_choices;
/* Empty = muted, * = all streams, ~ = existing custom mix. Otherwise opaque ID. */
static char ear_choice[2][64];

static struct rrclient_rx_stream *routing_snapshot(size_t *count) {
   *count = audio_rx_streams(NULL, 0);
   struct rrclient_rx_stream *streams = g_new0(struct rrclient_rx_stream, *count);
   audio_rx_streams(streams, *count);

   return streams;
}

static bool routing_ear_matches(const char *choice, const struct rrclient_rx_stream *stream, bool right) {
   if (!strcmp(choice, "~")) {
      return stream->output == 'S' || stream->output == (right ? 'R' : 'L');
   }

   return !strcmp(choice, "*") || !strcmp(choice, stream->id);
}

static void routing_apply(const struct rrclient_rx_stream *streams, size_t count) {
   routing_updating = true;

   for (size_t i = 0 ; i < count ; i++) {
      bool left = routing_ear_matches(ear_choice[0], &streams[i], false);
      bool right = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))
         ? routing_ear_matches(ear_choice[1], &streams[i], true) : left;
      audio_set_rx_stream_route(streams[i].id, left ? (right ? 'S' : 'L') : (right ? 'R' : 'M'));
   }

   routing_updating = false;
}

static void routing_read_ear(const struct rrclient_rx_stream *streams, size_t count, unsigned ear) {
   size_t audible = 0;
   const char *id = "";

   for (size_t i = 0 ; i < count ; i++) {
      if (streams[i].output == 'S' || streams[i].output == (ear ? 'R' : 'L')) {
         audible++;
         id = streams[i].id;
      }
   }

   /* Keep a specific stream selected when it is temporarily the only stream. */
   if (audible > 1) {
      id = audible == count ? "*" : "~";
   } else if (audible == 1 && !strcmp(ear_choice[ear], "*")) {
      id = "*";
   }
   g_strlcpy(ear_choice[ear], id, sizeof(ear_choice[ear]));
}

static gint routing_compare(gconstpointer a, gconstpointer b, gpointer unused) {
   (void)unused;
   const struct rrclient_rx_stream *left = a, *right = b;
   int result = strcmp(left->server, right->server);

   if (!result) {
      result = strcmp(left->room, right->room);
   }

   return result ? result : strcmp(left->uuid, right->uuid);
}

static gboolean routing_refresh(gpointer unused) {
   (void)unused;
   routing_source = 0;
   size_t count;
   struct rrclient_rx_stream *streams = routing_snapshot(&count);

   if (!count) {
      rr_floating_panel_hide(routing_panel);
      have_choices = false;
      inventory_dirty = false;
      g_free(streams);

      return G_SOURCE_REMOVE;
   }

   if (inventory_dirty && have_choices) {
      /* New subscriptions follow the user's ear choices; disappearance mutes that ear rather than silently replacing it with a different receiver. */
      routing_apply(streams, count);
      audio_rx_streams(streams, count);
   }
   inventory_dirty = false;
   g_qsort_with_data(streams, count, sizeof(*streams), routing_compare, NULL);
   routing_updating = true;
   routing_read_ear(streams, count, 0);
   routing_read_ear(streams, count, 1);
   bool asymmetric = false;

   for (size_t i = 0 ; i < count ; i++) {
      asymmetric |= streams[i].output == 'L' || streams[i].output == 'R';
   }

   if (asymmetric || strcmp(ear_choice[0], ear_choice[1])) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), TRUE);
   } else if (!have_choices) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), FALSE);
   }

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT(ear_combo[ear]);
      gtk_combo_box_text_remove_all(combo);
      gtk_combo_box_text_append(combo, "", "Muted");
      gtk_combo_box_text_append(combo, "*", "All streams");

      if (!strcmp(ear_choice[ear], "~")) {
         gtk_combo_box_text_append(combo, "~", "Custom mix (tab routing)");
      }

      for (size_t i = 0 ; i < count ; i++) {
         /* Show the room/path users recognize, not the internal stream UUID or another copy of the site name. IDs remain opaque combo keys. */
         const char *path = streams[i].room;

         if (*path == '#' || *path == '&') {
            path++;
         }
         char *label = g_strdup(*path ? path : streams[i].server);
         gtk_combo_box_text_append(combo, streams[i].id, label);
         g_free(label);
      }

      gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), ear_choice[ear]);
      char *label = gtk_combo_box_text_get_active_text(combo);
      gtk_widget_set_tooltip_text(ear_combo[ear], label);
      g_free(label);
   }

   gtk_widget_set_sensitive(ear_combo[1], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   gtk_widget_set_sensitive(ear_volume[1], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx)));
   routing_updating = false;
   have_choices = true;

   /* Showing an already visible window would undo minimization on some WMs. */
   rr_floating_panel_show(routing_panel);
   g_free(streams);

   return G_SOURCE_REMOVE;
}

static void routing_queue(void) {
   if (!routing_source) {
      routing_source = g_idle_add(routing_refresh, NULL);
   }
}

static void routing_volume_changed(GtkRange *range, gpointer user) {
   if (volume_updating) {
      return;
   }
   unsigned ear = GPOINTER_TO_UINT(user);
   int percent = (int)gtk_range_get_value(range);
   audio_set_rx_channel_volume(ear, percent);

   if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))) {
      volume_updating = true;
      gtk_range_set_value(GTK_RANGE(ear_volume[1 - ear]), percent);
      audio_set_rx_channel_volume(1 - ear, percent);
      volume_updating = false;
   }
}

static void routing_changed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;

   if (routing_updating) {
      return;
   }
   bool split = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx));

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ear_combo[split ? ear : 0]));
      g_strlcpy(ear_choice[ear], id ? id : "", sizeof(ear_choice[ear]));
   }

   routing_updating = true;

   if (!split) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(ear_combo[1]), ear_choice[0]);
   }
   gtk_widget_set_sensitive(ear_combo[1], split);
   gtk_widget_set_sensitive(ear_volume[1], split);

   if (!split) {
      volume_updating = true;
      int percent = (int)gtk_range_get_value(GTK_RANGE(ear_volume[0]));
      gtk_range_set_value(GTK_RANGE(ear_volume[1]), percent);
      audio_set_rx_channel_volume(1, percent);
      volume_updating = false;
   }
   routing_updating = false;
   size_t count;
   struct rrclient_rx_stream *streams = routing_snapshot(&count);
   routing_apply(streams, count);
   g_free(streams);
   routing_queue();
}

static void routing_event(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)data;
   (void)connection;
   (void)unused;

   if (routing_updating) {
      return;
   }

   if (!strcmp(event, "client.audio.streams.changed")) {
      inventory_dirty = true;
   } else {
      /* A later explicit tab/TUI edit wins over a queued inventory refresh. */
      inventory_dirty = false;
   }
   routing_queue();
}

void gtk_audio_routing_init(void) {
   if (routing_window) {
      return;
   }

   if (!main_window || !GTK_IS_BIN(main_window)) {
      return;
   }
   /* Put a floating panel *inside* the existing main window, preserving its notebook and all shortcut/focus handling. */
   GtkWidget *content = gtk_bin_get_child(GTK_BIN(main_window));

   if (!content) {
      return;
   }
   g_object_ref(content);
   gtk_container_remove(GTK_CONTAINER(main_window), content);
   routing_overlay = gtk_overlay_new();
   gtk_container_add(GTK_CONTAINER(routing_overlay), content);
   g_object_unref(content);
   gtk_container_add(GTK_CONTAINER(main_window), routing_overlay);
   routing_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), "RX Audio Routing", "RX Audio Routing");

   if (!routing_panel) {
      return;
   }
   routing_window = rr_floating_panel_widget(routing_panel);
   routing_body = rr_floating_panel_body(routing_panel);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, TRUE, TRUE, 0);

   GtkWidget *rig_volume = create_volbox();
   gtk_box_pack_start(GTK_BOX(routing_body), rig_volume, FALSE, FALSE, 2);
   split_rx = gtk_check_button_new_with_mnemonic("_Split RX");

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new_with_mnemonic(ear ? "_Right:" : "_Left:");
      ear_combo[ear] = gtk_combo_box_text_new();
      gtk_label_set_mnemonic_widget(GTK_LABEL(label), ear_combo[ear]);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      /* Long server/channel names must not change the fixed window width. */
      GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(ear_combo[ear]));

      if (cells) {
         g_object_set(cells->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 16, "max-width-chars", 16, NULL);
      }
      g_list_free(cells);
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 1, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_combo[ear], 1, ear + 1, 1, 1);
      g_signal_connect(ear_combo[ear], "changed", G_CALLBACK(routing_changed), NULL);
   }

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new(ear ? "Right Vol" : "Left Vol");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 110, -1);
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 3, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear + 3, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   gtk_box_pack_start(GTK_BOX(routing_body), split_rx, FALSE, FALSE, 2);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);
   gtk_widget_set_sensitive(ear_volume[1], FALSE);

   gtk_widget_show_all(routing_overlay);
   rr_floating_panel_hide(routing_panel);
   streams_token = event_on_token("client.audio.streams.changed", routing_event, NULL);
   routes_token = event_on_token("client.audio.route.changed", routing_event, NULL);
   inventory_dirty = true;
   routing_queue();
}

void gtk_audio_routing_shutdown(void) {
   if (streams_token) {
      event_off_token(streams_token);
      streams_token = 0;
   }

   if (routes_token) {
      event_off_token(routes_token);
      routes_token = 0;
   }

   if (routing_source) {
      g_source_remove(routing_source);
      routing_source = 0;
   }

   rr_floating_panel_destroy(routing_panel);
   routing_panel = NULL;
   rx_vol_slider = NULL;
   rx_rig_vol_slider = NULL;
   ear_volume[0] = ear_volume[1] = NULL;
   routing_window = routing_overlay = routing_body = split_rx = ear_combo[0] = ear_combo[1] = NULL;
   routing_updating = inventory_dirty = have_choices = false;
   memset(ear_choice, 0, sizeof(ear_choice));
}

// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/audio.graph.h>
#include <rrclient/gtk/gtk.graph.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.vol-box.h>
#include "gtk.audio-routing.h"
#include "gtk.floating-panel.h"
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_combo[2], *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
static GtkWidget *graph_window, *graph_canvas;
static guint routing_source;
static guint routing_watchdog_source;
static gint64 snapshot_sent_at;
static rr_event_token_t streams_token, routes_token, show_token, snapshot_token, graph_token;
static rr_audio_graph_snapshot_t *graph_snapshot;
static GArray *routing_cache;
static bool snapshot_waiting, routing_explicit_show;
static bool routing_updating, inventory_dirty, have_choices;
/* Empty = muted, * = all streams, ~ = existing custom mix. Otherwise opaque ID. */
static char ear_choice[2][64];

static struct rrclient_rx_stream *routing_snapshot(size_t *count) {
   *count = routing_cache ? routing_cache->len : 0;
   struct rrclient_rx_stream *streams = g_new0(struct rrclient_rx_stream, *count);
   if (*count)
      memcpy(streams, routing_cache->data, *count * sizeof(*streams));
   return streams;
}

static bool routing_ear_matches(const char *choice, const struct rrclient_rx_stream *stream, bool right) {
   if (!strcmp(choice, "~")) {
      return stream->output == 'S' || stream->output == (right ? 'R' : 'L');
   }

   return !strcmp(choice, "*") || !strcmp(choice, stream->id);
}

static void routing_apply(struct rrclient_rx_stream *streams, size_t count) {
   routing_updating = true;

   for (size_t i = 0 ; i < count ; i++) {
      bool left = routing_ear_matches(ear_choice[0], &streams[i], false);
      bool right = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))
         ? routing_ear_matches(ear_choice[1], &streams[i], true) : left;
      char output = left ? (right ? 'S' : 'L') : (right ? 'R' : 'M');
      audio_request_rx_stream_route(streams[i].id, output);
      streams[i].output = output;
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

/* Initial integrated audio graph view. It renders GTK-owned inventory
 * snapshots only; fwdsp, mixer and supervisor never run in this window. */
static void routing_queue(void);
static gboolean routing_graph_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)widget; (void)unused;
   if (!graph_snapshot) return FALSE;
   const rr_audio_graph_snapshot_t *g = graph_snapshot;
   if (g->node_count > RR_AUDIO_GRAPH_MAX_NODES || g->link_count > RR_AUDIO_GRAPH_MAX_LINKS) return FALSE;
   rr_gtk_graph_node_t nodes[RR_AUDIO_GRAPH_MAX_NODES] = {{0}};
   rr_gtk_graph_edge_t edges[RR_AUDIO_GRAPH_MAX_LINKS] = {{0}};
   guint source_index = 0;
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *n = &g->nodes[i];
      gboolean source = n->type == RR_AG_SOURCE;
      const char *route = n->route == 'L' ? "Left" : n->route == 'R' ? "Right" :
                          n->route == 'M' ? "Muted" : "Stereo";
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = source ? 25 : n->type == RR_AG_SINK ? 650 : 355,
         .y = source ? 20 + 108 * source_index++ : 80,
         .title = n->id,
         .subtitle = source ? route : n->type == RR_AG_SINK ? "fwdsp output" : "Core PCM mixer",
         .failed = n->health == RR_AG_FAILED,
         .bypass_pins = TRUE
      };
   }
   for (guint i = 0; i < g->link_count; i++)
      edges[i] = (rr_gtk_graph_edge_t) {
         .from = g->links[i].src, .to = g->links[i].dst,
         .kind = g->links[i].kind == RR_AG_BYPASS ? RR_GTK_GRAPH_BYPASS : RR_GTK_GRAPH_NORMAL
      };
   rr_gtk_graph_draw(cr, nodes, g->node_count, edges, g->link_count);
   return FALSE;
}
static void routing_graph_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length != sizeof(rr_audio_graph_snapshot_t))
      return;
   const rr_audio_graph_snapshot_t *incoming = message->payload;
   if (incoming->node_count > RR_AUDIO_GRAPH_MAX_NODES ||
       incoming->link_count > RR_AUDIO_GRAPH_MAX_LINKS)
      return;
   if (!graph_snapshot)
      graph_snapshot = g_new0(rr_audio_graph_snapshot_t, 1);
   *graph_snapshot = *incoming;
   if (graph_canvas) {
      guint sources = graph_snapshot->node_count > 2 ? graph_snapshot->node_count - 2 : 0;
      gtk_widget_set_size_request(graph_canvas, 850, MAX(480, 32 + 108 * sources));
      gtk_widget_queue_draw(graph_canvas);
   }
}
static gboolean routing_graph_hide(GtkWidget *widget, GdkEvent *event, gpointer unused) {
   (void)event;
   (void)unused;
   gtk_widget_hide(widget);
   return TRUE;
}
static void routing_graph_show(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   if (!graph_window) {
      graph_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(graph_window), "RustyRig Audio Graph");
      gtk_window_set_default_size(GTK_WINDOW(graph_window), 870, 590);
      if (GTK_IS_WINDOW(main_window))
         gtk_window_set_transient_for(GTK_WINDOW(graph_window), GTK_WINDOW(main_window));
      g_signal_connect(graph_window, "delete-event", G_CALLBACK(routing_graph_hide), NULL);
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
      gtk_container_add(GTK_CONTAINER(graph_window), box);
      GtkWidget *info = gtk_label_new("Current RX topology (read-only). Amber BYPASS pins are reserved for supervised fallback connections.");
      gtk_label_set_xalign(GTK_LABEL(info), 0);
      gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 5);
      GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      graph_canvas = gtk_drawing_area_new();
      gtk_widget_set_size_request(graph_canvas, 850, MAX(480, 32 + 108 * MIN(routing_cache ? routing_cache->len : 0u, 32u)));
      gtk_container_add(GTK_CONTAINER(scroller), graph_canvas);
      gtk_box_pack_start(GTK_BOX(box), scroller, TRUE, TRUE, 0);
      g_signal_connect(graph_canvas, "draw", G_CALLBACK(routing_graph_draw), NULL);
   }
   gtk_widget_show_all(graph_window);
   routing_queue(); /* Request an up-to-date core graph, not a stale UI cache. */
   if (graph_canvas)
      gtk_widget_queue_draw(graph_canvas);
}

static gboolean routing_refresh(gpointer unused) {
   (void)unused;
   routing_source = 0;
   size_t count;
   struct rrclient_rx_stream *streams = routing_snapshot(&count);

   /* A temporary empty inventory is not a reason to close the controls.
    * In particular, the first snapshot can arrive before any RX streams. */
   if (!count && !routing_explicit_show && !gtk_widget_get_visible(routing_window)) {
      have_choices = false;
      inventory_dirty = false;
      g_free(streams);
      return G_SOURCE_REMOVE;
   }

   if (inventory_dirty && have_choices) {
      /* New subscriptions follow the user's ear choices; disappearance mutes that ear rather than silently replacing it with a different receiver. */
      routing_apply(streams, count);
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
      if (!count)
         gtk_combo_box_text_append(combo, "no-streams", "No RX streams connected");

      if (!strcmp(ear_choice[ear], "~")) {
         gtk_combo_box_text_append(combo, "~", "Custom mix (tab routing)");
      }

      for (size_t i = 0 ; i < count ; i++) {
         /* Show the room/path users recognize, not the internal stream UUID or another copy of the site name. IDs remain opaque combo keys. */
         const char *path = streams[i].room;

         /* Local SDRs share &local-sdr, but each bridge is a distinct VFO. */
         if (!strcmp(path, "&local-sdr") && !strncmp(streams[i].id, "local.sdr.", 10)) {
            path = streams[i].id;
         } else if (*path == '#' || *path == '&') {
            path++;
         }
         char *label = g_strdup(*path ? path : streams[i].server);
         gtk_combo_box_text_append(combo, streams[i].id, label);
         g_free(label);
      }

      gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), count ? ear_choice[ear] : "no-streams");
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

static void routing_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;
   if (!message || !message->payload || message->length < sizeof(rr_audio_stream_snapshot_t))
      return;
   const rr_audio_stream_snapshot_t *snapshot = message->payload;
   if (snapshot->count > 128 ||
       message->length != sizeof(*snapshot) + (size_t)snapshot->count * sizeof(struct rrclient_rx_stream))
      return;
   if (!routing_cache)
      routing_cache = g_array_new(FALSE, FALSE, sizeof(struct rrclient_rx_stream));
   g_array_set_size(routing_cache, 0);
   if (snapshot->count)
      g_array_append_vals(routing_cache, snapshot->streams, snapshot->count);
   snapshot_waiting = false;
   snapshot_sent_at = 0;
   if (!routing_source)
      routing_source = g_idle_add(routing_refresh, NULL);
}
/* A request can be sent before audio_init() installs the core handler.
 * Never let that leave the routing panel permanently waiting for a reply. */
static void routing_queue(void) {
   if (snapshot_waiting && g_get_monotonic_time() - snapshot_sent_at < G_USEC_PER_SEC)
      return;
   snapshot_waiting = true;
   snapshot_sent_at = g_get_monotonic_time();
   const guint8 request_marker = 1;
   rr_event_message_t request = {
      .name = RRCLIENT_AUDIO_SNAPSHOT_REQUEST,
      .payload = &request_marker, .length = sizeof(request_marker)
   };
   event_emit_message(&request);
}

static gboolean routing_watchdog(gpointer unused) {
   (void)unused;
   /* Retry only an unanswered request; normal updates are event-driven.
    * Polling visible panels unnecessarily could redisplay a hidden panel. */
   if (routing_panel && snapshot_waiting)
      routing_queue();
   return G_SOURCE_CONTINUE;
}

/* Local RX playback gain, separate from per-ear routing and rig speaker volume. */
static void routing_rx_gain_changed(GtkRange *range, gpointer unused) {
   (void)unused;
   audio_request_rx_volume((int)gtk_range_get_value(range));
}

static void routing_volume_changed(GtkRange *range, gpointer user) {
   if (volume_updating) {
      return;
   }
   unsigned ear = GPOINTER_TO_UINT(user);
   int percent = (int)gtk_range_get_value(range);
   audio_request_rx_channel_volume(ear, percent);

   if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))) {
      volume_updating = true;
      gtk_range_set_value(GTK_RANGE(ear_volume[1 - ear]), percent);
      audio_request_rx_channel_volume(1 - ear, percent);
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
      audio_request_rx_channel_volume(1, percent);
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

static void routing_show_event(const char *event, const char *data, rrconn_t *connection, void *context) {
   (void)event;
   (void)data;
   (void)connection;
   (void)context;

   if (routing_panel) {
      routing_explicit_show = true;
      rr_floating_panel_restore(routing_panel);
      rr_floating_panel_show(routing_panel);
      routing_queue();
   }
}

/* Reparent the standard label/slider row into the shared routing grid. A single GTK grid column gives every slider identical alignment and width. */
static void routing_grid_volume_row(GtkWidget *grid, GtkWidget *row, gint grid_row) {
   GList *children = gtk_container_get_children(GTK_CONTAINER(row));

   if (children && children->next) {
      GtkWidget *label = GTK_WIDGET(children->data);
      GtkWidget *slider = GTK_WIDGET(children->next->data);
      g_object_ref(label);
      g_object_ref(slider);
      gtk_container_remove(GTK_CONTAINER(row), label);
      gtk_container_remove(GTK_CONTAINER(row), slider);
      gtk_grid_attach(GTK_GRID(grid), label, 0, grid_row, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), slider, 1, grid_row, 1, 1);
      gtk_widget_set_hexpand(slider, TRUE);
      g_object_unref(label);
      g_object_unref(slider);
   }
   g_list_free(children);
   gtk_widget_destroy(row);
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
   rr_floating_panel_set_closable(routing_panel, FALSE);
   routing_window = rr_floating_panel_widget(routing_panel);
   routing_body = rr_floating_panel_body(routing_panel);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, TRUE, TRUE, 0);

   /* Client-side playback gain is separate from the rig's hardware speaker level. */
   GtkWidget *gain_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
   GtkWidget *gain_label = gtk_label_new("RX Gain");
   gtk_widget_set_halign(gain_label, GTK_ALIGN_START);
   gtk_widget_set_size_request(gain_label, 72, -1);
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_vol_slider), FALSE);
   gtk_widget_set_size_request(rx_vol_slider, 110, -1);
   gtk_widget_set_hexpand(rx_vol_slider, TRUE);
   gtk_widget_set_tooltip_text(rx_vol_slider, "Client RX playback gain (independent of rig speaker volume)");
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), CLAMP(cfg_get_int("audio.volume.rx", 30), 0, 100));
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(routing_rx_gain_changed), NULL);
   gtk_box_pack_start(GTK_BOX(gain_row), gain_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(gain_row), rx_vol_slider, TRUE, TRUE, 0);
   routing_grid_volume_row(grid, gain_row, 5);

   GtkWidget *graph_button = gtk_button_new_with_label("Audio Graph…");
   g_signal_connect(graph_button, "clicked", G_CALLBACK(routing_graph_show), NULL);
   gtk_grid_attach(GTK_GRID(grid), graph_button, 0, 7, 2, 1);

   GtkWidget *rig_volume = create_volbox();
   routing_grid_volume_row(grid, rig_volume, 6);
   split_rx = gtk_check_button_new_with_mnemonic("_Split RX");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 0, 0, 2, 1);

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
      gtk_widget_set_hexpand(ear_volume[ear], TRUE);
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 3, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear + 3, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);
   gtk_widget_set_sensitive(ear_volume[1], FALSE);

   gtk_widget_show_all(routing_overlay);
   rr_floating_panel_hide(routing_panel);
   show_token = event_on_domain_token("client.audio.routing.show", routing_show_event, NULL, "gtk");
   streams_token = event_on_domain_token("client.audio.streams.changed", routing_event, NULL, "gtk");
   routes_token = event_on_domain_token("client.audio.route.changed", routing_event, NULL, "gtk");
   snapshot_token = event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_RESULT, routing_snapshot_event, NULL, "gtk");
   graph_token = event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT, routing_graph_snapshot_event, NULL, "gtk");
   inventory_dirty = true;
   routing_watchdog_source = g_timeout_add(1500, routing_watchdog, NULL);
   routing_queue();
}

void gtk_audio_routing_shutdown(void) {
   if (routing_watchdog_source) {
      g_source_remove(routing_watchdog_source);
      routing_watchdog_source = 0;
   }
   if (graph_token) {
      event_off_token(graph_token);
      graph_token = 0;
   }
   g_clear_pointer(&graph_snapshot, g_free);
   if (graph_window) {
      gtk_widget_destroy(graph_window);
      graph_window = graph_canvas = NULL;
   }
   if (snapshot_token) {
      event_off_token(snapshot_token);
      snapshot_token = 0;
   }
   if (routing_cache) {
      g_array_free(routing_cache, TRUE);
      routing_cache = NULL;
   }
   snapshot_waiting = false;
   snapshot_sent_at = 0;
   routing_explicit_show = false;
   if (show_token) {
      event_off_token(show_token);
      show_token = 0;
   }

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

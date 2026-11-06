// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <math.h>
#include <librustyaxe/json.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/audio.graph.h>
#include <rrclient/gtk/gtk.graph.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.audio-routing.h"
#include "gtk.floating-panel.h"
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
static RRFloatPanel *graph_panel;
static GtkWidget *graph_canvas;
static rr_audio_graph_snapshot_t *graph_snapshot;
/* Fit the topology to the actual viewport. Panel zoom and wheel zoom
 * multiply independently; neither forces a large GTK size requisition. */
static double graph_zoom = 1.0, graph_wheel_zoom = 1.0;
static double graph_scale = 1.0, graph_offset_x, graph_offset_y;
static void graph_canvas_update_size(void) {
   if (graph_canvas)
      gtk_widget_queue_draw(graph_canvas);
}
static void routing_graph_zoom(double scale, gpointer unused) {
   (void)unused;
   graph_zoom = scale;
   graph_canvas_update_size();
}
static void routing_graph_bounds(double *width, double *height) {
   guint sources = 0;
   if (graph_snapshot)
      for (guint i = 0; i < graph_snapshot->node_count; i++)
         sources += graph_snapshot->nodes[i].type == RR_AG_SOURCE;
   *width = 850.0;
   *height = MAX(180.0, 36.0 + 140.0 * sources);
}
static gboolean routing_graph_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
   (void)widget; (void)unused;
   double step = 0.0;
   if (event->direction == GDK_SCROLL_UP) step = 1.0;
   else if (event->direction == GDK_SCROLL_DOWN) step = -1.0;
   else if (event->direction == GDK_SCROLL_SMOOTH) step = -event->delta_y;
   if (!step) return FALSE;
   graph_wheel_zoom = CLAMP(graph_wheel_zoom * pow(1.12, step), 0.10, 6.0);
   graph_canvas_update_size();
   return TRUE;
}

static guint routing_source;
static guint routing_watchdog_source;
static gint64 snapshot_sent_at;
static rr_event_token_t show_token, snapshot_token, graph_token;
static GArray *routing_cache;
static bool snapshot_waiting;

/* Ear selections are sets of stream IDs; a stream may be in both ears. */
static struct rrclient_rx_stream *routing_snapshot(size_t *count) {
   *count = routing_cache ? routing_cache->len : 0;
   struct rrclient_rx_stream *streams = g_new0(struct rrclient_rx_stream, *count);
   if (*count) memcpy(streams, routing_cache->data, *count * sizeof(*streams));
   return streams;
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
   rr_gtk_graph_edge_t edges[RR_AUDIO_GRAPH_MAX_LINKS * 2] = {{0}};
   guint source_index = 0;
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *n = &g->nodes[i];
      gboolean source = n->type == RR_AG_SOURCE;
      const char *route = n->route == 'L' ? "Left" : n->route == 'R' ? "Right" :
                          n->route == 'M' ? "Muted" : "Stereo";
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = source ? 25 : n->type == RR_AG_SINK ? 650 : 355,
         .y = source ? 20 + 140 * source_index++ : 80,
         .title = n->id,
         .subtitle = source ? route : n->type == RR_AG_SINK ? "fwdsp output" : "Core PCM mixer",
         .failed = n->health == RR_AG_FAILED,
         .bypass_pins = n->type == RR_AG_PROCESSOR, .stereo_pins = TRUE,
         .io = n->type == RR_AG_SOURCE ? RR_GTK_GRAPH_IO_SOURCE :
               n->type == RR_AG_SINK ? RR_GTK_GRAPH_IO_SINK : RR_GTK_GRAPH_IO_BOTH,
         .fwdsp = n->type == RR_AG_SINK && !strcmp(n->id, "sink.client.dsp0")
      };
   }
   guint edge_count = 0;
   for (guint i = 0; i < g->link_count; i++) {
      const rr_ag_view_link_t *link = &g->links[i];
      if (link->src >= g->node_count || link->dst >= g->node_count)
         continue;
      rr_gtk_graph_edge_kind_t kind = link->kind == RR_AG_BYPASS ?
         RR_GTK_GRAPH_BYPASS : RR_GTK_GRAPH_NORMAL;
      if (kind == RR_GTK_GRAPH_BYPASS) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind
         };
         continue;
      }
      if (link->channels & RR_AG_CHANNEL_LEFT)
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind,
            .src_pin = RR_GTK_GRAPH_PIN_LEFT, .dst_pin = RR_GTK_GRAPH_PIN_LEFT
         };
      if (link->channels & RR_AG_CHANNEL_RIGHT)
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind,
            .src_pin = RR_GTK_GRAPH_PIN_RIGHT, .dst_pin = RR_GTK_GRAPH_PIN_RIGHT
         };
   }
   double natural_w, natural_h;
   routing_graph_bounds(&natural_w, &natural_h);
   double available_w = MAX(1, gtk_widget_get_allocated_width(widget));
   double available_h = MAX(1, gtk_widget_get_allocated_height(widget));
   double fit = MIN(available_w / natural_w, available_h / natural_h);
   graph_scale = MAX(0.01, fit * graph_zoom * graph_wheel_zoom);
   graph_offset_x = MAX(0.0, (available_w - natural_w * graph_scale) / 2.0);
   graph_offset_y = MAX(0.0, (available_h - natural_h * graph_scale) / 2.0);
   cairo_save(cr);
   cairo_translate(cr, graph_offset_x, graph_offset_y);
   cairo_scale(cr, graph_scale, graph_scale);
   rr_gtk_graph_draw(cr, nodes, g->node_count, edges, edge_count);
   cairo_restore(cr);
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
   graph_canvas_update_size();
}
/* The pipeline viewer remains a separate process. A GStreamer crash in the
 * inspector must not affect the client, its radio controls or audio engine. */
static void routing_pipeline_viewer_done(GObject *source, GAsyncResult *result, gpointer unused) {
   (void)unused;
   GError *error = NULL;
   gchar *out = NULL, *err = NULL;
   if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &out, &err, &error)) {
      g_warning("Read-only pipeline viewer failed: %s", error ? error->message : "unknown error");
      g_clear_error(&error);
   }
   g_free(out);
   g_free(err);
   g_object_unref(source);
}
static void routing_pipeline_open(const char *id) {
   if (g_strcmp0(id, "sink.client.dsp0")) return;
   const char *launch = cfg_get("pipeline:sink.client.mix");
   if (!launch || !*launch) {
      g_warning("No configured GStreamer pipeline for sink.client.dsp0");
      return;
   }
   dict *document = dict_new();
   if (!document) return;
   gboolean valid = !dict_add_uint(document, "pipelines.count", 1) &&
      !dict_add(document, "pipelines.0.id", "sink.client.mix") &&
      !dict_add(document, "pipelines.0.launch", launch);
   gchar *json = valid ? dict2json(document) : NULL;
   dict_free(document);
   if (!json) return;
   gchar *program = g_find_program_in_path("rrpipelines");
   if (!program && g_file_test("bin/rrpipelines", G_FILE_TEST_IS_EXECUTABLE))
      program = g_strdup("bin/rrpipelines");
   if (!program) {
      g_warning("Cannot find rrpipelines to inspect %s", id);
      g_free(json);
      return;
   }
   const gchar *argv[] = { program, "--read-only", NULL };
   GError *error = NULL;
   GSubprocess *process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE |
      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &error);
   if (process)
      g_subprocess_communicate_utf8_async(process, json, NULL, routing_pipeline_viewer_done, NULL);
   else {
      g_warning("Unable to launch pipeline viewer: %s", error ? error->message : "unknown error");
      g_clear_error(&error);
   }
   g_free(program);
   g_free(json);
}
static gboolean routing_graph_clicked(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget; (void)unused;
   if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY ||
       !graph_snapshot) return FALSE;
   /* Convert pointer coordinates to the graph's unscaled model space. */
   double gx = (event->x - graph_offset_x) / graph_scale;
   double gy = (event->y - graph_offset_y) / graph_scale;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if (n->type != RR_AG_SINK || strcmp(n->id, "sink.client.dsp0")) continue;
      double x = 650, y = 80;
      if (gx >= x && gx <= x + RR_GTK_GRAPH_NODE_W &&
          gy >= y && gy <= y + RR_GTK_GRAPH_NODE_H) {
         routing_pipeline_open(n->id);
         return TRUE;
      }
   }
   return FALSE;
}
static void routing_graph_close(gpointer unused) {
   (void)unused;
   if (graph_panel)
      rr_floating_panel_hide(graph_panel);
}
void gtk_audio_graph_present(void) {
   if (!routing_overlay || !GTK_IS_NOTEBOOK(main_notebook))
      return;
   if (!graph_panel) {
      graph_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay),
         GTK_NOTEBOOK(main_notebook), "Audio Graph", "Audio Graph");
      if (!graph_panel)
         return;
      rr_floating_panel_set_close_callback(graph_panel, routing_graph_close, NULL);
      rr_floating_panel_set_minimizable(graph_panel, FALSE);
      rr_floating_panel_set_resizable(graph_panel, TRUE);
      rr_floating_panel_set_zoom_callback(graph_panel, routing_graph_zoom, NULL);
      GtkWidget *box = rr_floating_panel_body(graph_panel);
      GtkWidget *info = gtk_label_new("Current RX topology: cyan = left, purple = right; rounded fwdsp blocks open read-only pipelines.");
      gtk_label_set_xalign(GTK_LABEL(info), 0);
      gtk_box_pack_start(GTK_BOX(box), info, FALSE, FALSE, 5);
      GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
      /* The graph has its own Cairo zoom. Never scale its viewport size
       * request through the generic widget wrapper as well. */
      g_object_set_data(G_OBJECT(scroller), "rr-scale-exempt", GINT_TO_POINTER(1));
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
      graph_canvas = gtk_drawing_area_new();
      gtk_widget_set_size_request(graph_canvas, 1, 1);
      gtk_widget_set_hexpand(graph_canvas, TRUE);
      gtk_widget_set_vexpand(graph_canvas, TRUE);
      g_object_set_data(G_OBJECT(graph_canvas), "rr-scale-exempt", GINT_TO_POINTER(1));
      gtk_widget_set_size_request(scroller, 320, 240);
      gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), 240);
      gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 160);
      gtk_widget_set_hexpand(scroller, TRUE);
      gtk_widget_set_vexpand(scroller, TRUE);
      graph_canvas_update_size();
      gtk_container_add(GTK_CONTAINER(scroller), graph_canvas);
      gtk_box_pack_start(GTK_BOX(box), scroller, TRUE, TRUE, 0);
      gtk_widget_add_events(graph_canvas, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
      g_signal_connect(graph_canvas, "scroll-event", G_CALLBACK(routing_graph_scroll), NULL);
      g_signal_connect(graph_canvas, "button-press-event", G_CALLBACK(routing_graph_clicked), NULL);
      g_signal_connect(graph_canvas, "draw", G_CALLBACK(routing_graph_draw), NULL);
   }
   rr_floating_panel_restore(graph_panel);
   rr_floating_panel_show(graph_panel);
   routing_queue(); /* Request an up-to-date core graph, not a stale UI cache. */
   if (graph_canvas)
      gtk_widget_queue_draw(graph_canvas);
}

static gboolean routing_refresh(gpointer unused) {
   (void)unused;
   routing_source = 0;
   /* Volume only: live routing changes belong to the Audio Graph. */
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
   (void)widget; (void)unused;
   gboolean split = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx));
   gtk_widget_set_sensitive(ear_volume[1], split);
   if (!split) {
      volume_updating = TRUE;
      int percent = (int)gtk_range_get_value(GTK_RANGE(ear_volume[0]));
      gtk_range_set_value(GTK_RANGE(ear_volume[1]), percent);
      audio_request_rx_channel_volume(1, percent);
      volume_updating = FALSE;
   }
}

static void routing_show_event(const char *event, const char *data, rrconn_t *connection, void *context) {
   (void)event;
   (void)data;
   (void)connection;
   (void)context;

   if (routing_panel) {
      rr_floating_panel_restore(routing_panel);
      rr_floating_panel_show(routing_panel);
      routing_queue();
   }
}

void gtk_volume_toggle(void) {
   if (!routing_panel || !routing_window) return;
   if (gtk_widget_get_visible(routing_window))
      rr_floating_panel_hide(routing_panel);
   else {
      rr_floating_panel_restore(routing_panel);
      rr_floating_panel_show(routing_panel);
   }
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
   routing_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), "Volume", "Volume");

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
   gtk_grid_attach(GTK_GRID(grid), gain_label, 0, 3, 1, 1);
   gtk_grid_attach(GTK_GRID(grid), rx_vol_slider, 1, 3, 1, 1);

   split_rx = gtk_check_button_new_with_mnemonic("_Split RX");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 0, 0, 2, 1);

   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      GtkWidget *label = gtk_label_new(ear ? "Right Vol" : "Left Vol");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 110, -1);
      gtk_widget_set_hexpand(ear_volume[ear], TRUE);
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear + 1, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear + 1, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);
   gtk_widget_set_sensitive(ear_volume[1], FALSE);

   gtk_widget_show_all(routing_overlay);
   rr_floating_panel_hide(routing_panel);
   show_token = event_on_domain_token("client.audio.routing.show", routing_show_event, NULL, "gtk");
   snapshot_token = event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_RESULT, routing_snapshot_event, NULL, "gtk");
   graph_token = event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT, routing_graph_snapshot_event, NULL, "gtk");
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
   if (graph_panel) {
      rr_floating_panel_destroy(graph_panel);
      graph_panel = NULL;
      graph_canvas = NULL;
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
   if (show_token) {
      event_off_token(show_token);
      show_token = 0;
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
   routing_window = routing_overlay = routing_body = split_rx = NULL;
}

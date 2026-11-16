// RustyRig GTK RX routing window. Licensed under the project's MIT/GPL terms.
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <math.h>
#include <stdio.h>
#include <rrclient/local.modem.h>
#include <rrclient/local.bridge.h>
#include <rrclient/sercom.h>
#include <librustyaxe/json.h>
#include <librustyaxe/config.h>
#include <rrclient/audio.h>
#include <rrclient/audio.graph.h>
#include <rrclient/gtk/gtk.graph.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/database.h>
#include "gtk.audio-routing.h"
#include "gtk.floating-panel.h"
#include <rrpipelines/editor.h>
#ifdef RR_HAVE_GSTREAMER
#include <gst/gst.h>
#endif
/* Uses the Config editor's validated cfg merge and dirty-state handling. */
extern gboolean gtk_pipeline_editor_apply_output(GtkWidget *, const gchar *, gchar **);
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
/* Embedded tool page: deliberately omitted from the visible main tab strip. */
static GtkWidget *graph_page;
static GtkWidget *graph_canvas;
static GtkWidget *audio_tabs, *pipeline_editor_widget;
static char *graph_focus_id;
static GtkWidget *graph_profiles;
static GtkWidget *graph_profile_load_button, *graph_profile_save_button, *graph_profile_remove_button;
static gboolean graph_profile_updating;
static void graph_profile_modified(void);
static void graph_profile_update_buttons(void);
static rr_audio_graph_snapshot_t *graph_snapshot;
static rr_audio_stream_snapshot_t *routing_rx_snapshot;
struct routing_compact_control;
static void routing_compact_sync(void);
static void routing_compact_draw(cairo_t *cr);
static gboolean routing_compact_hit(double x, double y, gboolean double_click);
static gboolean routing_compact_drag_motion(double x, double y);
static gboolean routing_compact_drag_release(double x, double y);
static void routing_compact_restore(void);
static void routing_compact_save(const struct routing_compact_control *control);
static gboolean graph_gain_at(double x, double y, gint *source, gint *channel, gboolean *port);
static gboolean routing_controls_visible = TRUE;
static gint routing_control_drag_index = -1;
static void routing_compact_bounds(double *width, double *height);
static void routing_effect_refresh(void);
static void routing_effect_draw(cairo_t *cr);
static void routing_effect_bounds(double *width, double *height);
static gboolean routing_effect_press(double x, double y, gboolean dbl);
static gboolean routing_effect_motion(double x, double y);
static gboolean routing_effect_release(void);
static void routing_show_controls_toggled(GtkToggleButton *toggle, gpointer unused);
/* Preserve display order through transient disconnects. Only GTK owns this history: the core graph and audio routing never contain retired nodes. */
#define GRAPH_GHOST_US (15 * G_USEC_PER_SEC)
static gint64 graph_missing_since[RR_AUDIO_GRAPH_MAX_NODES];
/* Fit when practical, but never shrink the default canvas below 80% just to squeeze a wide patchbay into a small window. Wheel/panel zoom may still zoom further out. */
static double graph_zoom = 1.0, graph_wheel_zoom = 1.0;
static double graph_scale = 1.0, graph_offset_x, graph_offset_y;
/* Horizontal panning cannot expose space before the graph's left edge.
 * Zero is the auto-centered starting position when the graph fits. */
static double graph_pan_x, graph_pan_y;
static void routing_pan_clamp_left(void) {
   graph_pan_x = MIN(0., graph_pan_x);
}
static gboolean graph_pan_dragging;
static double graph_pan_begin_x, graph_pan_begin_y, graph_pan_base_x, graph_pan_base_y;
static void graph_canvas_update_size(void) {
   if (graph_canvas) {
      gtk_widget_queue_draw(graph_canvas);
   }
}
/* Layout is GTK-owned and is never part of the executable audio graph.
 * Only user-moved positions are stored; default positions follow topology. */
typedef struct { double x, y; gboolean pinned; } routing_position_t;
static GHashTable *graph_positions;
static gint graph_node_drag = -1;
static double graph_node_drag_x, graph_node_drag_y;
static double graph_node_press_x, graph_node_press_y;
static gboolean graph_node_moved;
/* The snap setting affects only interactive node movement. Stored coordinates
 * remain exact, and Shift temporarily bypasses snapping for fine placement. */
#define GRAPH_LAYOUT_MARGIN 28.0
#define GRAPH_SNAP_STEP 20.0
#define GRAPH_SNAP_KEY "audio.graph.snap-grid"
static gboolean graph_snap_enabled;
static void routing_snap_toggled(GtkToggleButton *button, gpointer unused) {
   (void)unused;
   graph_snap_enabled = gtk_toggle_button_get_active(button);
   rrclient_database_setting_set(GRAPH_SNAP_KEY,
                                  graph_snap_enabled ? "true" : "false");
}
static double routing_snap_coordinate(double value) {
   return CLAMP(GRAPH_LAYOUT_MARGIN +
                round((value - GRAPH_LAYOUT_MARGIN) / GRAPH_SNAP_STEP) *
                   GRAPH_SNAP_STEP, 0., 100000.);
}
#define GRAPH_LAYOUT_GAP_X 355.0
#define GRAPH_LAYOUT_GAP_Y 200.0

static char *routing_position_key(const char *id) {
   char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
   char *key = g_strdup_printf("audio.graph.layout.%s", hash);
   g_free(hash);
   return key;
}
static routing_position_t *routing_position(const char *id) {
   if (!id || !*id) return NULL;
   if (!graph_positions)
      graph_positions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   routing_position_t *pos = g_hash_table_lookup(graph_positions, id);
   if (pos) return pos;
   pos = g_new0(routing_position_t, 1);
   char *key = routing_position_key(id);
   char *saved = rrclient_database_setting_get(key);
   if (saved && *saved) {
      char *end = NULL;
      double x = g_ascii_strtod(saved, &end);
      if (end != saved && *end == ',') {
         char *y_start = end + 1;
         double y = g_ascii_strtod(y_start, &end);
         if (end != y_start && !*end && isfinite(x) && isfinite(y) &&
             x >= 0 && x <= 100000 && y >= 0 && y <= 100000) {
            pos->x = x; pos->y = y; pos->pinned = TRUE;
         }
      }
   }
   g_free(saved);
   g_free(key);
   g_hash_table_insert(graph_positions, g_strdup(id), pos);
   return pos;
}
static void routing_position_save(const char *id, const routing_position_t *pos) {
   char *key = routing_position_key(id);
   char x[G_ASCII_DTOSTR_BUF_SIZE], y[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(x, sizeof(x), "%.2f", pos->x);
   g_ascii_formatd(y, sizeof(y), "%.2f", pos->y);
   char *value = g_strdup_printf("%s,%s", x, y);
   rrclient_database_setting_set(key, value);
   g_free(value);
   g_free(key);
}
static double routing_node_x(const rr_ag_view_node_t *node) {
   routing_position_t *pos = routing_position(node->id);
   return pos ? pos->x : GRAPH_LAYOUT_MARGIN;
}
static double routing_node_y(const rr_ag_view_node_t *node) {
   routing_position_t *pos = routing_position(node->id);
   return pos ? pos->y : GRAPH_LAYOUT_MARGIN;
}
static void routing_layout_set(const rr_ag_view_node_t *node, double x, double y,
                               gboolean reset) {
   routing_position_t *pos = routing_position(node->id);
   if (!pos || (pos->pinned && !reset)) return;
   pos->x = x;
   pos->y = y;
   if (reset) {
      pos->pinned = FALSE;
      char *key = routing_position_key(node->id);
      rrclient_database_setting_set(key, "");
      g_free(key);
   }
}
/* Spacious left-to-right columns. Every downstream bus is vertically centered on its
 * inputs, while isolated capture devices are placed in a separate lane. */
static void routing_layout_auto(gboolean reset) {
   if (!graph_snapshot) return;
   guint legacy = 0, high = 0;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if (n->type != RR_AG_SOURCE) continue;
      if (!strcmp(n->id, "src.client.dsp0")) continue;
      else if (n->downstream_rate_hz > 16000) high++;
      else legacy++;
   }
   guint legacy_row = 0, high_row = 0, isolated_row = 0;
   double low_y = GRAPH_LAYOUT_MARGIN +
      0.5 * (legacy ? legacy - 1 : 0) * GRAPH_LAYOUT_GAP_Y;
   /* The direct-HQ inputs share the converter's horizontal column, not its
    * vertical lane. Leave room so they cannot cover rx16.resample. */
   const double high_start_y = GRAPH_LAYOUT_MARGIN +
      MAX(legacy, 1) * GRAPH_LAYOUT_GAP_Y + 22.;
   const double high_center_y = high_start_y +
      0.5 * (high ? high - 1 : 0) * GRAPH_LAYOUT_GAP_Y;
   double hq_y = high ? (low_y + high_center_y) / 2. : low_y;
   if (!legacy && high) hq_y = high_center_y;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double x, y;
      if (n->type == RR_AG_SOURCE) {
         if (!strcmp(n->id, "src.client.dsp0")) {
            x = GRAPH_LAYOUT_MARGIN;
            y = high_start_y + high * GRAPH_LAYOUT_GAP_Y + 50. +
                isolated_row++ * GRAPH_LAYOUT_GAP_Y;
         } else if (n->downstream_rate_hz > 16000) {
            x = GRAPH_LAYOUT_MARGIN + 2 * GRAPH_LAYOUT_GAP_X;
            y = high_start_y + high_row++ * GRAPH_LAYOUT_GAP_Y;
         } else {
            x = GRAPH_LAYOUT_MARGIN;
            y = GRAPH_LAYOUT_MARGIN + legacy_row++ * GRAPH_LAYOUT_GAP_Y;
         }
      } else if (!strcmp(n->id, "rx.mix")) {
         x = GRAPH_LAYOUT_MARGIN + GRAPH_LAYOUT_GAP_X; y = low_y;
      } else if (!strcmp(n->id, "rx16.resample")) {
         x = GRAPH_LAYOUT_MARGIN + 2 * GRAPH_LAYOUT_GAP_X; y = low_y;
      } else if (!strcmp(n->id, "hq.mix")) {
         x = GRAPH_LAYOUT_MARGIN + 3 * GRAPH_LAYOUT_GAP_X; y = hq_y;
      } else if (n->type == RR_AG_SINK) {
         if (g_str_has_prefix(n->id, "modem.")) {
            x = GRAPH_LAYOUT_MARGIN + GRAPH_LAYOUT_GAP_X;
            y = high_start_y + (high + isolated_row++) * GRAPH_LAYOUT_GAP_Y;
         } else {
            x = GRAPH_LAYOUT_MARGIN + 4 * GRAPH_LAYOUT_GAP_X; y = hq_y;
         }
      } else {
         x = GRAPH_LAYOUT_MARGIN + 2 * GRAPH_LAYOUT_GAP_X; y = hq_y;
      }
      routing_layout_set(n, x, y, reset);
   }
}
static void routing_graph_bounds(double *width, double *height) {
   double max_x = 1080, max_y = 280;
   if (graph_snapshot) {
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
         max_x = MAX(max_x, routing_node_x(n) + RR_GTK_GRAPH_NODE_W + GRAPH_LAYOUT_MARGIN);
         max_y = MAX(max_y, routing_node_y(n) + RR_GTK_GRAPH_NODE_H + GRAPH_LAYOUT_MARGIN);
      }
   }
   /* Control nodes occupy the same view coordinates but not the audio graph.
    * Hidden controls must not influence canvas zoom/auto-fit. */
   if (routing_controls_visible) routing_compact_bounds(&max_x, &max_y);
   if (routing_controls_visible) routing_effect_bounds(&max_x, &max_y);
   *width = max_x;
   *height = max_y;
}
/* Mouse wheel retains zoom, smooth touchpad scroll pans. Shift+wheel
 * pans horizontally and Ctrl+wheel explicitly zooms on either device. */
static gboolean routing_graph_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
   (void)unused;
   double dx = 0., dy = 0.;
   switch (event->direction) {
      case GDK_SCROLL_UP: dy = -1.; break;
      case GDK_SCROLL_DOWN: dy = 1.; break;
      case GDK_SCROLL_LEFT: dx = -1.; break;
      case GDK_SCROLL_RIGHT: dx = 1.; break;
      case GDK_SCROLL_SMOOTH: dx = event->delta_x; dy = event->delta_y; break;
      default: return FALSE;
   }
   if (!(event->state & GDK_CONTROL_MASK) &&
       (event->direction == GDK_SCROLL_SMOOTH ||
        (event->state & GDK_SHIFT_MASK) || dx)) {
      if (event->state & GDK_SHIFT_MASK) graph_pan_x -= (dx + dy) * 48.;
      else {
         graph_pan_x -= dx * 48.;
         graph_pan_y -= dy * 48.;
      }
      routing_pan_clamp_left();
      graph_canvas_update_size();
      return TRUE;
   }
   double step = -dy;
   if (!step) step = -dx;
   if (!step) return FALSE;
   /* Preserve the graph point below the pointer while changing the zoom. */
   double px = event->x, py = event->y;
   double before_x = (px - graph_offset_x) / MAX(.01, graph_scale);
   double before_y = (py - graph_offset_y) / MAX(.01, graph_scale);
   graph_wheel_zoom = CLAMP(graph_wheel_zoom * pow(1.12, step), .10, 6.);
   double w, h;
   routing_graph_bounds(&w, &h);
   double aw = MAX(1, gtk_widget_get_allocated_width(widget));
   double ah = MAX(1, gtk_widget_get_allocated_height(widget));
   double fit = MIN(aw / w, ah / h);
   double next_scale = MAX(.01, MAX(.80, fit) * graph_zoom * graph_wheel_zoom);
   graph_pan_x = px - before_x * next_scale -
       MAX(0., (aw - w * next_scale) / 2.);
   graph_pan_y = py - before_y * next_scale -
       MAX(0., (ah - h * next_scale) / 2.);
   routing_pan_clamp_left();
   graph_canvas_update_size();
   return TRUE;
}

static guint routing_watchdog_source;
static gint64 snapshot_sent_at;
static rr_event_token_t show_token, snapshot_token, graph_token, routes_token;
static bool snapshot_waiting;

/* Initial integrated audio graph view. It renders GTK-owned inventory snapshots only; fwdsp, mixer and supervisor never run in this window. */
static void routing_queue(void);
static gboolean graph_pointer(double x, double y, double *gx, double *gy);
static gint graph_drag_source = -1, graph_drag_channel = -1;
static gint gain_drag_source = -1, gain_drag_channel = -1;
static gboolean gain_drag_port;
/* Shared source/mixer/sink slider geometry; leave 12px between tracks
 * and pins, so the port remains separately draggable. */
/* Compact L/R output trims reside inside each source block by its pins. */
#define GRAPH_GAIN_W 56.0
#define GRAPH_GAIN_MIN (-60.0)
#define GRAPH_GAIN_MAX 12.0
static void graph_gain_drag(double x);
static const char *pipeline_for_node(const char *id);
static void routing_focus_pipeline(const char *id);
static gboolean graph_drag_mixer, graph_drag_moved;
static gint graph_drag_modem_target = -1;
static gint graph_drag_rig_target = -1;
static gint graph_drag_lv2_target = -1;
static gboolean graph_drag_lv2_output;
static gint graph_drag_lv2_dest_channel;
static double graph_drag_start_x, graph_drag_start_y, graph_drag_x, graph_drag_y;
/* Locate a valid port on the opposite end of a patch cord. Use the
 * same hit/clock-domain rules for live preview and final connection. */
static gboolean routing_modem_slot(const rr_ag_view_node_t *n, unsigned *slot) {
   unsigned value = 0;
   char tail = 0;
   if (n->type != RR_AG_SINK ||
       sscanf(n->id, "modem.%u.rx%c", &value, &tail) != 1 ||
       value < 1 || value > RR_LOCAL_MODEM_MAX)
      return FALSE;
   *slot = value;
   return TRUE;
}
static gboolean routing_modem_source(const rr_ag_view_node_t *n) {
   return n->type == RR_AG_SOURCE && !n->read_only &&
          n->downstream_rate_hz <= 16000 && n->channels == 1 &&
          n->sample_format == RR_PCM_S16LE &&
          !g_str_has_prefix(n->id, "modem.");
}
static void routing_modem_connect(unsigned slot, const char *source) {
   rr_local_modem_request_t req = {
      .action = RR_LOCAL_MODEM_ROUTE, .slot = slot
   };
   g_strlcpy(req.source, source, sizeof(req.source));
   rr_local_modem_request(&req);
   routing_queue();
}
/* Audio sources can be dragged to modem RX inputs, and modem inputs can
 * be dragged back to a source. The modem path ignores speaker L/R routing. */
static gboolean routing_drag_target(double x, double y, gint *source_index,
                                    double *snap_x, double *snap_y) {
   if (!graph_snapshot || graph_drag_channel < 0) return FALSE;
   const gint origin = graph_drag_mixer ? -graph_drag_source - 1 : graph_drag_source;
   if (origin < 0 || (guint)origin >= graph_snapshot->node_count) return FALSE;
   const rr_ag_view_node_t *origin_node = &graph_snapshot->nodes[origin];
   unsigned modem_slot = 0;
   gboolean from_modem = graph_drag_mixer && routing_modem_slot(origin_node, &modem_slot);
   gboolean from_lv2 = !graph_drag_mixer &&
       g_str_has_prefix(origin_node->id, "proc.lv2.");
   gboolean from_lv2_input = graph_drag_mixer &&
       g_str_has_prefix(origin_node->id, "proc.lv2.");
   gboolean from_mic = !graph_drag_mixer &&
       !strcmp(origin_node->id, "src.client.dsp0");
   gboolean from_modem_tx = !graph_drag_mixer &&
       g_str_has_prefix(origin_node->id, "modem.") &&
       g_str_has_suffix(origin_node->id, ".tx");
   const char *required_bus = graph_drag_mixer ? origin_node->id :
       (origin_node->downstream_rate_hz > 16000 ? "hq.mix" : "rx.mix");
   const double limit = CLAMP(18. / MAX(.01, graph_scale), 14., 34.);
   double best = limit;
   gboolean found = FALSE;
   graph_drag_modem_target = -1;
   graph_drag_rig_target = -1;
   graph_drag_lv2_target = -1;
   graph_drag_lv2_output = FALSE;
   graph_drag_lv2_dest_channel = 0;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if (graph_missing_since[i]) continue;
      unsigned target_slot = 0;
      gboolean is_modem = routing_modem_slot(n, &target_slot);
      gboolean modem_connection = from_modem ?
         routing_modem_source(n) :
         (!graph_drag_mixer && routing_modem_source(origin_node) && is_modem);
      gboolean to_lv2 = !graph_drag_mixer &&
          routing_modem_source(origin_node) &&
          n->type == RR_AG_PROCESSOR &&
          g_str_has_prefix(n->id, "proc.lv2.");
      gboolean lv2_output = from_lv2 && n->type == RR_AG_MIXER &&
          !strcmp(n->id, "rx.mix");
      gboolean lv2_reverse_input = from_lv2_input && routing_modem_source(n);
      gboolean lv2_connection = to_lv2 || lv2_output || lv2_reverse_input;
      gboolean rig_target = (from_modem_tx || from_mic) &&
          n->type == RR_AG_SINK &&
          g_str_has_prefix(n->id, "rig.") &&
          g_str_has_suffix(n->id, ".tx");
      gboolean valid = rig_target || modem_connection || lv2_connection ||
         (!from_lv2 && !from_lv2_input && (graph_drag_mixer ?
          (n->type == RR_AG_SOURCE && !n->read_only &&
           !strcmp(n->downstream_rate_hz > 16000 ? "hq.mix" : "rx.mix",
                   required_bus)) :
          (n->type == RR_AG_MIXER && !strcmp(n->id, required_bus))));
      if (!valid) continue;
      gint dest_channel = graph_drag_channel;
      if (lv2_output) {
         double ny0 = routing_node_y(n);
         dest_channel = fabs(y - ny0 - 79.) < fabs(y - ny0 - 60.) ? 1 : 0;
      }
      double nx = routing_node_x(n) +
          (graph_drag_mixer ? RR_GTK_GRAPH_NODE_W : 0.);
      double ny = routing_node_y(n) + (rig_target ?
         (from_modem_tx && graph_drag_channel ?
          RR_GTK_GRAPH_NODE_H - 16. : 68.) :
         lv2_connection ? (lv2_output ? (dest_channel ? 79. : 60.) :
            (to_lv2 && n->channels == 2) ||
            (lv2_reverse_input && origin_node->channels == 2) ?
               (graph_drag_channel ? 79. : 60.) : 68.) :
         modem_connection ? (from_modem ? 60. : 68.) :
         (graph_drag_channel ? 79. : 60.));
      double dist = hypot(x - nx, y - ny);
      if (dist >= best) continue;
      best = dist;
      *source_index = graph_drag_mixer ? (gint)i : origin;
      *snap_x = nx;
      *snap_y = ny;
      graph_drag_modem_target = modem_connection ?
         (gint)(from_modem ? modem_slot : target_slot) : -1;
      graph_drag_rig_target = rig_target ? (gint)i : -1;
      if (lv2_connection) {
         const rr_ag_view_node_t *effect = to_lv2 ? n : origin_node;
         graph_drag_lv2_output = lv2_output;
         graph_drag_lv2_dest_channel = dest_channel;
         unsigned slot = 0;
         if (sscanf(effect->id, "proc.lv2.%u", &slot) == 1 &&
             slot >= 1 && slot <= RR_AUDIO_EFFECT_MAX)
            graph_drag_lv2_target = (gint)slot;
      }
      found = TRUE;
   }
   return found;
}
/* Some topology-locked nodes nevertheless have genuine mixer gain stages. */
static gboolean routing_gain_supported(const rr_ag_view_node_t *n) {
   /* Modem RX/TX and rig TX ports do not have controllable mixer
    * faders. In particular, showing +/- dB on a modem sink implies
    * a working gain stage that does not exist. */
   if (g_str_has_prefix(n->id, "modem.") ||
       (g_str_has_prefix(n->id, "rig.") &&
        g_str_has_suffix(n->id, ".tx"))) return FALSE;
   if (n->type == RR_AG_SOURCE)
      return !n->read_only || !strcmp(n->id, "legacy.speaker");
   if (n->type == RR_AG_MIXER || n->type == RR_AG_SINK) return TRUE;
   return n->type == RR_AG_PROCESSOR && !strcmp(n->id, "rx16.resample");
}
static gboolean routing_gain_input(const rr_ag_view_node_t *n) {
   return n->type == RR_AG_SOURCE || !strcmp(n->id, "rx16.resample");
}
/* UI rate strings describe the real core format or a requested hardware
 * boundary; never label the latter as negotiated GStreamer caps. */
static void routing_rate_label(guint32 rate, char *out, size_t size) {
   if (!rate) {
      g_strlcpy(out, "unknown", size);
   } else if (rate % 1000 == 0) {
      g_snprintf(out, size, "%uk", rate / 1000);
   } else if (rate % 100 == 0) {
      g_snprintf(out, size, "%u.%uk", rate / 1000, (rate % 1000) / 100);
   } else {
      g_snprintf(out, size, "%u Hz", rate);
   }
}
static gboolean routing_graph_draw(GtkWidget *widget, cairo_t *cr, gpointer unused) {
   (void)widget;
   (void)unused;

   if (!graph_snapshot) {
      return FALSE;
   }
   const rr_audio_graph_snapshot_t *g = graph_snapshot;

   if (g->node_count > RR_AUDIO_GRAPH_MAX_NODES || g->link_count > RR_AUDIO_GRAPH_MAX_LINKS) {
      return FALSE;
   }
   rr_gtk_graph_node_t nodes[RR_AUDIO_GRAPH_MAX_NODES] = { {
                                                              0
                                                           } };
   rr_gtk_graph_edge_t edges[RR_AUDIO_GRAPH_MAX_LINKS * 2] = { {
                                                                  0
                                                               } };
   char format_details[RR_AUDIO_GRAPH_MAX_NODES][96] = {{0}};
   char lv2_subtitles[RR_AUDIO_GRAPH_MAX_NODES][52] = {{0}};

   for (guint i = 0 ; i < g->node_count ; i++) {
      const rr_ag_view_node_t *n = &g->nodes[i];
      gboolean source = n->type == RR_AG_SOURCE;
      const char *route = n->route == 'L' ? "L" : n->route == 'R' ? "R" : n->route == 'M' ? "Muted" : "Stereo";
      char input[20], output[20];
      routing_rate_label(n->rate_hz, input, sizeof(input));
      routing_rate_label(n->downstream_rate_hz, output, sizeof(output));
      if (n->rate_hz && n->downstream_rate_hz && n->resampling) {
         g_snprintf(format_details[i], sizeof(format_details[i]), "%s -> %s%s",
                    input, output, n->rate_requested ? " requested" : " SRC");
      } else if (n->rate_hz) {
         g_snprintf(format_details[i], sizeof(format_details[i]), "%s %s%s",
                    input, n->channels == 2 ? "stereo" : "mono",
                    n->rate_requested ? " requested" : "");
      } else {
         g_strlcpy(format_details[i], "PCM format unknown", sizeof(format_details[i]));
      }
      if (source && !n->read_only) {
         /* A mono RX stream is fanned out to two independently routable
          * mixer ears. Show two destination pins, not a fake mono socket
          * next to two unrelated gain controls. */
         char *summary = g_strdup_printf("%s | %s%s", route,
            format_details[i], n->channels == 1 ? " -> L/R" : "");
         g_strlcpy(format_details[i], summary, sizeof(format_details[i]));
         g_free(summary);
      }
      if (g_str_has_prefix(n->id, "proc.lv2.") && n->rig[0]) {
         /* The full LV2 factory name often exceeds the 300px PCM node. */
         g_snprintf(lv2_subtitles[i], sizeof(lv2_subtitles[i]), "%.31s%s",
                    n->rig, strlen(n->rig) > 31 ? "…" : "");
      }
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = routing_node_x(n), .y = routing_node_y(n),
         /* The stable routing path is the bold headline. Receiver/device
          * description occupies the regular-weight line beneath it. */
         .title = n->id, .subtitle =
            lv2_subtitles[i][0] ? lv2_subtitles[i] :
            n->rig[0] ? n->rig :
            g_str_has_prefix(n->id, "modem.") ?
            (source ? "Modem TX audio / PTT" : "Modem RX audio") :
            g_str_has_prefix(n->id, "rig.") ? "Rig TX audio / PTT" :
            n->type == RR_AG_PROCESSOR ? "Audio processor" :
            n->type == RR_AG_SINK ? "GStreamer playback" :
            n->type == RR_AG_MIXER ? "Core PCM mixer" : "Audio source", .detail =
            graph_missing_since[i] ? "Disconnected" : format_details[i], .failed = n->health == RR_AG_FAILED, .stale =
            graph_missing_since[i] != 0,  .selected = graph_node_drag == (gint)i || !g_strcmp0(graph_focus_id, n->id), .bypass_pins = FALSE, .stereo_pins = n->channels == 2 || (source && !n->read_only), .io =
            n->type == RR_AG_SOURCE ? RR_GTK_GRAPH_IO_SOURCE : n->type == RR_AG_SINK ? RR_GTK_GRAPH_IO_SINK : RR_GTK_GRAPH_IO_BOTH, .gain_pins = !g_str_has_prefix(n->id, "modem.") && routing_gain_supported(n), .ptt_pin = g_str_has_prefix(n->id, "modem.") && g_str_has_suffix(n->id, ".tx"),
            .ptt_input = g_str_has_prefix(n->id, "rig.") && g_str_has_suffix(n->id, ".tx"), .fwdsp =
            (n->type == RR_AG_SINK && !strcmp(n->id, "sink.client.dsp0")) ||
            g_str_has_prefix(n->id, "proc.lv2.")
      };
   }

   guint edge_count = 0;

   for (guint i = 0 ; i < g->link_count ; i++) {
      const rr_ag_view_link_t *link = &g->links[i];

      if (link->src >= g->node_count || link->dst >= g->node_count) {
         continue;
      }
      rr_gtk_graph_edge_kind_t kind =
         link->kind == RR_AG_BYPASS ? RR_GTK_GRAPH_BYPASS :
         link->kind == RR_AG_CONTROL ? RR_GTK_GRAPH_CONTROL : RR_GTK_GRAPH_NORMAL;

      if (kind == RR_GTK_GRAPH_BYPASS || kind == RR_GTK_GRAPH_CONTROL) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind
         };
         continue;
      }

      if (link->channels & RR_AG_CHANNEL_LEFT) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind, .src_pin = RR_GTK_GRAPH_PIN_LEFT, .dst_pin = RR_GTK_GRAPH_PIN_LEFT
         };
      }

      if (link->channels & RR_AG_CHANNEL_RIGHT) {
         edges[edge_count++] = (rr_gtk_graph_edge_t) {
            .from = link->src, .to = link->dst, .kind = kind, .src_pin = RR_GTK_GRAPH_PIN_RIGHT, .dst_pin = RR_GTK_GRAPH_PIN_RIGHT
         };
      }
   }

   double natural_w, natural_h;
   routing_graph_bounds(&natural_w, &natural_h);
   double available_w = MAX(1, gtk_widget_get_allocated_width(widget));
   double available_h = MAX(1, gtk_widget_get_allocated_height(widget));
   double fit = MIN(available_w / natural_w, available_h / natural_h);
   /* Keep pointer coordinates stable throughout a block drag; changes to
    * layout bounds must not rescale the canvas mid-gesture. */
   if (graph_node_drag < 0 && routing_control_drag_index < 0) {
      graph_scale = MAX(0.01, MAX(.80, fit) * graph_zoom * graph_wheel_zoom);
      graph_offset_x = MAX(0.0, (available_w - natural_w * graph_scale) / 2.0) + graph_pan_x;
      graph_offset_y = MAX(0.0, (available_h - natural_h * graph_scale) / 2.0) + graph_pan_y;
   }
   cairo_save(cr);
   cairo_translate(cr, graph_offset_x, graph_offset_y);
   cairo_scale(cr, graph_scale, graph_scale);
   rr_gtk_graph_draw(cr, nodes, g->node_count, edges, edge_count);

   /* Output trims inside each source block; backed by the existing
    * per-stream L/R gains rather than by speaker/master volume. */
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];
      if (!routing_gain_input(node) || !routing_gain_supported(node)) continue;
      double y0 = routing_node_y(node);
      if (graph_missing_since[i]) continue;
      for (unsigned channel = 0; channel < 2; channel++) {
         double y = y0 + (channel ? 79 : 60);
         double db = CLAMP(node->input_gain_db[channel],
                           GRAPH_GAIN_MIN, GRAPH_GAIN_MAX);
         double ratio = (db - GRAPH_GAIN_MIN) / (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN);
         cairo_set_line_width(cr, 1.8);
         cairo_set_source_rgba(cr, .55, .67, .76, .85);
         double gain_x = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
         cairo_move_to(cr, gain_x, y);
         cairo_line_to(cr, gain_x + GRAPH_GAIN_W, y);
         cairo_stroke(cr);
         cairo_set_source_rgb(cr, .31, .82, .97);
         cairo_arc(cr, gain_x + GRAPH_GAIN_W * ratio, y, 3.6, 0, 2 * G_PI);
         cairo_fill(cr);
         /* Keep the complete label, dB value, slider and output pin in
          * one compact horizontal row: L OUT +0dB [---*---] #. */
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8.4);
         cairo_move_to(cr, routing_node_x(node) + 15, y + 3);
         cairo_show_text(cr, node->type == RR_AG_PROCESSOR ?
            (channel ? "R IN" : "L IN") : (channel ? "R OUT" : "L OUT"));
         char value[20];
         g_snprintf(value, sizeof(value), "%+.0fdB", db);
         cairo_text_extents_t label_size;
         cairo_text_extents(cr, value, &label_size);
         cairo_move_to(cr, gain_x - 6. - label_size.width, y + 3);
         cairo_show_text(cr, value);
      }
      /* Source diagnostics are read-only and originate from the running
       * mixer status; they are not the negotiated sound-device buffer. */
      char status[80];
      g_snprintf(status, sizeof(status), "Q:%ums D:%" G_GUINT64_FORMAT " U:%" G_GUINT64_FORMAT,
                 node->queued_ms, node->dropped_frames, node->starvation_events);
      cairo_set_source_rgb(cr, .72, .80, .84);
      cairo_set_font_size(cr, 8);
      cairo_move_to(cr, routing_node_x(node) + 7, y0 + 101);
      cairo_show_text(cr, status);
   }

   /* Two more independent DSP stages: mixer output trims and sink input
    * trims. Both sit inside their corresponding blocks by the L/R pins. */
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];
      if (node->type != RR_AG_MIXER && node->type != RR_AG_SINK) continue;
      double node_x = routing_node_x(node);
      double x = node_x + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      if (graph_missing_since[i] || !routing_gain_supported(node)) continue;
      for (unsigned channel = 0; channel < 2; channel++) {
         double y = routing_node_y(node) + (channel ? 79 : 60);
         double db = CLAMP(node->port_gain_db[channel],
                           GRAPH_GAIN_MIN, GRAPH_GAIN_MAX);
         double ratio = (db - GRAPH_GAIN_MIN) / (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN);
         cairo_set_line_width(cr, 1.8);
         cairo_set_source_rgba(cr, .55, .67, .76, .85);
         cairo_move_to(cr, x, y);
         cairo_line_to(cr, x + GRAPH_GAIN_W, y);
         cairo_stroke(cr);
         cairo_set_source_rgb(cr, .31, .82, .97);
         cairo_arc(cr, x + GRAPH_GAIN_W * ratio, y, 3.6, 0, 2 * G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8.4);
         char value[20];
         g_snprintf(value, sizeof(value), "%+.0fdB", db);
         cairo_move_to(cr, node_x + 15, y + 3);
         cairo_show_text(cr, node->type == RR_AG_SINK ?
            (channel ? "R IN" : "L IN") : (channel ? "R OUT" : "L OUT"));
         cairo_text_extents_t label_size;
         cairo_text_extents(cr, value, &label_size);
         cairo_move_to(cr, x - 6. - label_size.width, y + 3);
         cairo_show_text(cr, value);
      }
   }

   /* Draw control wires first, then the LV2 pins and backed labels.
    * The text must remain readable when a wire crosses behind it. */
   routing_compact_draw(cr);
   routing_effect_draw(cr);

   /* The preview always ends in a filled arrowhead. As it approaches
    * a valid receiving ring the endpoint snaps to that exact pin. */
   if (graph_drag_channel >= 0) {
      const gint idx = graph_drag_mixer ? -graph_drag_source - 1 : graph_drag_source;
      if (idx >= 0 && (guint)idx < g->node_count) {
         const rr_ag_view_node_t *n = &g->nodes[idx];
         double ox = routing_node_x(n) +
             (graph_drag_mixer ? 0. : RR_GTK_GRAPH_NODE_W);
         double oy = routing_node_y(n) +
             (g_str_has_prefix(n->id, "proc.lv2.") && n->channels != 2 ? 68. :
             (!graph_drag_mixer && g_str_has_prefix(n->id, "modem.") &&
               g_str_has_suffix(n->id, ".tx") ?
               (graph_drag_channel ? RR_GTK_GRAPH_NODE_H - 16 : 68.) :
              graph_drag_mixer && g_str_has_prefix(n->id, "modem.") ? 68. :
              graph_drag_channel ? 79. : 60.));
         gint source_index = -1;
         double snap_x = graph_drag_x, snap_y = graph_drag_y;
         gboolean snap = routing_drag_target(graph_drag_x, graph_drag_y,
                                             &source_index, &snap_x, &snap_y);
         double sx = graph_drag_mixer ? snap_x : ox;
         double sy = graph_drag_mixer ? snap_y : oy;
         double ex = graph_drag_mixer ? ox : snap_x;
         double ey = graph_drag_mixer ? oy : snap_y;
         /* Leave the ring empty so an arrow cannot obscure the socket. */
         if (snap) ex -= 9.;
         double cx = (sx + ex) / 2.;
         gboolean ptt_preview = !graph_drag_mixer &&
             g_str_has_prefix(n->id, "modem.") &&
             g_str_has_suffix(n->id, ".tx") && graph_drag_channel;
         cairo_set_source_rgba(cr, ptt_preview ? .96 : snap ? .45 : (graph_drag_channel ? .73 : .34),
                                ptt_preview ? .20 : snap ? .95 : (graph_drag_channel ? .48 : .78),
                                ptt_preview ? .20 : snap ? .65 : (graph_drag_channel ? 1. : .93), .95);
         const double dash[] = {5, 4};
         cairo_set_dash(cr, dash, 2, 0);
         cairo_set_line_width(cr, 2.5);
         cairo_move_to(cr, sx, sy);
         cairo_curve_to(cr, cx, sy, cx, ey, ex, ey);
         cairo_stroke(cr);
         cairo_set_dash(cr, NULL, 0, 0);
         /* Free-moving tip follows the cable; snapped tip enters the
          * destination socket horizontally, left-to-right. */
         double dx = snap ? 1. : ex - sx;
         double dy = snap ? 0. : ey - sy;
         double length = hypot(dx, dy);
         if (length > .001) {
            dx /= length; dy /= length;
            cairo_move_to(cr, ex, ey);
            cairo_line_to(cr, ex - 9.*dx - 4.8*dy, ey - 9.*dy + 4.8*dx);
            cairo_line_to(cr, ex - 9.*dx + 4.8*dy, ey - 9.*dy - 4.8*dx);
            cairo_close_path(cr);
            cairo_fill(cr);
         }
      }
   }
   cairo_restore(cr);

   return FALSE;
}

static void routing_modem_engine_changed(GtkComboBox *engine, gpointer unused) {
   (void)unused;
   GtkWidget *mode = g_object_get_data(G_OBJECT(engine), "rr-mode");
   GtkWidget *value = g_object_get_data(G_OBJECT(engine), "rr-value");
   if (!mode || !value) return;
   const char *selected = gtk_combo_box_get_active_id(engine);
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(mode));
   if (!g_strcmp0(selected, "ft8")) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), "auto", "Auto");
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 0, 0);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 0);
      gtk_widget_set_sensitive(mode, FALSE);
      gtk_widget_set_sensitive(value, FALSE);
   } else if (!g_strcmp0(selected, "olivia")) {
      const char *tones[] = {"4", "8", "16", "32", "64"};
      for (unsigned i=0; i<G_N_ELEMENTS(tones); i++)
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), tones[i], tones[i]);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(mode), "8");
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 125, 2000);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 500);
      gtk_widget_set_sensitive(mode, TRUE);
      gtk_widget_set_sensitive(value, TRUE);
   } else {
      const char *modes[] = {"BPSK31", "QPSK31", "BPSK63", "BPSK125", "QPSK63", "QPSK125"};
      for (unsigned i=0; i<G_N_ELEMENTS(modes); i++)
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(mode), modes[i], modes[i]);
      gtk_spin_button_set_range(GTK_SPIN_BUTTON(value), 100, 3500);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(value), 1500);
      gtk_widget_set_sensitive(mode, TRUE);
      gtk_widget_set_sensitive(value, TRUE);
   }
   if (gtk_combo_box_get_active(GTK_COMBO_BOX(mode)) < 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
}
static void routing_new_modem(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   GtkWidget *dlg = gtk_dialog_new_with_buttons("New Modem",
      GTK_IS_WINDOW(main_window) ? GTK_WINDOW(main_window) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Create", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *body = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(body), grid, FALSE, FALSE, 0);
   GtkWidget *engine = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "ft8", "FT8");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "psk", "PSK31 / PSK");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(engine), "olivia", "Olivia");
   gtk_combo_box_set_active(GTK_COMBO_BOX(engine), 0);
   GtkWidget *source = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source), "unconnected", "Disconnected — connect later");
   if (graph_snapshot) for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if (routing_modem_source(n)) {
         const char *name = n->rig[0] ? n->rig : n->id;
         char *display = NULL;
         /* Show the stable local SDR/VFO ID and its catalog identity;
          * the selected value remains the opaque PCM stream ID. */
         if (g_str_has_prefix(n->id, "local.sdr.") &&
             n->id[10] >= 'A' && n->id[10] <= 'Z' && !n->id[11]) {
            rr_local_bridge_ui_state_t receiver = {0};
            if (rrclient_local_bridge_ui_state_vfo(n->id[10], &receiver)) {
               if (receiver.receiver_name[0] && receiver.receiver_location[0])
                  display = g_strdup_printf("%s — %s (%s)", n->id,
                              receiver.receiver_name, receiver.receiver_location);
               else if (receiver.receiver_name[0] || receiver.receiver_location[0])
                  display = g_strdup_printf("%s — %s", n->id,
                              receiver.receiver_name[0] ? receiver.receiver_name :
                              receiver.receiver_location);
            }
            if (!display) display = g_strdup(n->id);
         }
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source), n->id,
                                   display ? display : name);
         g_free(display);
      }
   }
   gtk_combo_box_set_active(GTK_COMBO_BOX(source), 0);
   GtkWidget *mode = gtk_combo_box_text_new();
   GtkWidget *value = gtk_spin_button_new_with_range(0, 3500, 1);
   g_object_set_data(G_OBJECT(engine), "rr-mode", mode);
   g_object_set_data(G_OBJECT(engine), "rr-value", value);
   g_signal_connect(engine, "changed", G_CALLBACK(routing_modem_engine_changed), NULL);
   routing_modem_engine_changed(GTK_COMBO_BOX(engine), NULL);
   GtkWidget *labels[] = {gtk_label_new("Modem"), gtk_label_new("RX source"),
                          gtk_label_new("Mode / tones"), gtk_label_new("Audio Hz / bandwidth")};
   GtkWidget *fields[] = {engine, source, mode, value};
   for (unsigned i = 0; i < 4; i++) {
      gtk_widget_set_halign(labels[i], GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), labels[i], 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
   }
   gtk_widget_show_all(dlg);
   if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
      const char *selected_engine = gtk_combo_box_get_active_id(GTK_COMBO_BOX(engine));
      const char *selected_source = gtk_combo_box_get_active_id(GTK_COMBO_BOX(source));
      const char *selected_mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(mode));
      if (selected_engine && selected_source && selected_mode) {
         rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_START};
         g_strlcpy(req.engine, selected_engine, sizeof(req.engine));
         if (strcmp(selected_source, "unconnected"))
            g_strlcpy(req.source, selected_source, sizeof(req.source));
         g_strlcpy(req.option, selected_mode, sizeof(req.option));
         req.value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(value));
         if (!strcmp(selected_engine, "ft8")) {
            g_strlcpy(req.option, "auto", sizeof(req.option));
            req.value = 0;
         } else if (!strcmp(selected_engine, "olivia") &&
                    req.value != 125 && req.value != 250 && req.value != 500 &&
                    req.value != 1000 && req.value != 2000) {
            req.value = 500;
         }
         rr_local_modem_request(&req);
      }
   }
   gtk_widget_destroy(dlg);
}
static void routing_modem_menu_action(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-slot"));
   unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "modem-action"));
   if (slot < 1 || slot > RR_LOCAL_MODEM_MAX) return;
   if (action == 1) {
      rr_local_modem_request_t req = {.slot=slot};
      req.action = RR_LOCAL_MODEM_ROUTE; rr_local_modem_request(&req);
      req.action = RR_LOCAL_MODEM_TX_ROUTE; rr_local_modem_request(&req);
      req.action = RR_LOCAL_MODEM_PTT_ROUTE; rr_local_modem_request(&req);
   } else if (action == 2) {
      rr_local_modem_request_t req = {.action=RR_LOCAL_MODEM_STOP, .slot=slot};
      rr_local_modem_request(&req);
   }
   routing_queue();
}
static gboolean routing_modem_context_menu(GdkEventButton *event, double x, double y) {
   if (!graph_snapshot) return FALSE;
   for (gint i=(gint)graph_snapshot->node_count - 1; i>=0; i--) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      if (graph_missing_since[i] || !g_str_has_prefix(n->id, "modem.")) continue;
      double nx = routing_node_x(n), ny = routing_node_y(n);
      if (x < nx || x > nx+RR_GTK_GRAPH_NODE_W ||
          y < ny || y > ny+RR_GTK_GRAPH_NODE_H) continue;
      unsigned slot=0;
      if (sscanf(n->id, "modem.%u.", &slot) != 1) continue;
      GtkWidget *menu = gtk_menu_new();
      const char *titles[] = {"Disconnect All", "Remove Modem"};
      for (unsigned j=0; j<2; j++) {
         GtkWidget *item = gtk_menu_item_new_with_label(titles[j]);
         g_object_set_data(G_OBJECT(item), "modem-slot", GUINT_TO_POINTER(slot));
         g_object_set_data(G_OBJECT(item), "modem-action", GUINT_TO_POINTER(j+1));
         g_signal_connect(item, "activate", G_CALLBACK(routing_modem_menu_action), NULL);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      }
      g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
      return TRUE;
   }
   return FALSE;
}
static void routing_graph_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length != sizeof(rr_audio_graph_snapshot_t)) {
      return;
   }
   const rr_audio_graph_snapshot_t *incoming = message->payload;

   if (incoming->node_count > RR_AUDIO_GRAPH_MAX_NODES || incoming->link_count > RR_AUDIO_GRAPH_MAX_LINKS) {
      return;
   }

   gint64 now = g_get_monotonic_time();
   rr_audio_graph_snapshot_t *merged = g_new0(rr_audio_graph_snapshot_t, 1);
   gint64 missing[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   guint incoming_to_display[RR_AUDIO_GRAPH_MAX_NODES];
   gboolean matched[RR_AUDIO_GRAPH_MAX_NODES] = {
      FALSE
   };
   guint ghosts = 0;
   gboolean topology_changed = FALSE;

   /* Reuse slots by stable node ID, keeping their screen locations unchanged. A vanished source becomes an inert grey ghost until the grace period expires.
    * Reserve capacity for every live node before keeping ghosts. */
   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         const rr_ag_view_node_t *old = &graph_snapshot->nodes[i];
         guint j = 0;

         for ( ; j < incoming->node_count ; j++) {
            if (!matched[j] && incoming->nodes[j].type == old->type && !strcmp(incoming->nodes[j].id, old->id)) {
               break;
            }
         }

         if (j < incoming->node_count) {
            guint k = merged->node_count++;
            merged->nodes[k] = incoming->nodes[j];
            incoming_to_display[j] = k;
            matched[j] = TRUE;
         } else {
            gint64 since = graph_missing_since[i] ? graph_missing_since[i] : now;

            if (now - since < GRAPH_GHOST_US && incoming->node_count + ghosts < RR_AUDIO_GRAPH_MAX_NODES) {
               guint k = merged->node_count++;
               merged->nodes[k] = *old;
               missing[k] = since;
               ghosts++;

               if (!graph_missing_since[i]) {
                  topology_changed = TRUE;
               }
            } else {
               topology_changed = TRUE;
            }
         }
      }
   }

   for (guint j = 0 ; j < incoming->node_count ; j++) {
      if (!matched[j]) {
         guint k = merged->node_count++;
         merged->nodes[k] = incoming->nodes[j];
         incoming_to_display[j] = k;
         topology_changed = TRUE;
      }
   }

   /* Only active links are drawn. A ghost must never remain connected to the mixer, even if the route in its last snapshot was Stereo. */
   for (guint i = 0 ; i < incoming->link_count ; i++) {
      rr_ag_view_link_t link = incoming->links[i];

      if (link.src >= incoming->node_count || link.dst >= incoming->node_count) {
         continue;
      }
      link.src = incoming_to_display[link.src];
      link.dst = incoming_to_display[link.dst];
      merged->links[merged->link_count++] = link;
   }

   if (topology_changed) {
      graph_node_drag = -1;
      if (graph_drag_channel >= 0) {
         graph_drag_source = graph_drag_channel = -1;
         graph_drag_mixer = graph_drag_moved = FALSE;
      }
      gain_drag_source = gain_drag_channel = -1;
      gain_drag_port = FALSE;
   }
   g_free(graph_snapshot);
   graph_snapshot = merged;
   memcpy(graph_missing_since, missing, sizeof(missing));
   routing_compact_sync();
   /* A periodic topology refresh must never steal a node from the pointer
    * while the user is dragging it. Reflow after drag release instead. */
   if (graph_node_drag < 0) routing_layout_auto(FALSE);
   graph_canvas_update_size();
   graph_profile_update_buttons();
}

/* The isolated fwdsp worker remains the only executor. GStreamer is used
 * in rrclient's GTK editor for graph composition and property inspection. */
static const char *pipeline_for_node(const char *id) {
   if (!g_strcmp0(id, "sink.client.dsp0")) return "sink.client.mix";
   if (!g_strcmp0(id, "src.client.dsp0")) return "src.client.dsp0";
   return NULL;
}
static const char *node_for_pipeline(const char *id) {
   if (!g_strcmp0(id, "sink.client.mix")) return "sink.client.dsp0";
   if (!g_strcmp0(id, "src.client.dsp0")) return "src.client.dsp0";
   return NULL;
}
static void routing_focus_node(const char *id) {
   if (!id || !audio_tabs) return;
   g_free(graph_focus_id);
   graph_focus_id = g_strdup(id);
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 0);
   graph_canvas_update_size();
}
static void pipeline_element_focused(const gchar *pipeline, gpointer user) {
   (void)user;
   const char *node = node_for_pipeline(pipeline);
   if (node) routing_focus_node(node);
}
static gboolean pipeline_apply(const gchar *json, gpointer user) {
   (void)user;
   GtkWidget *parent = audio_tabs ? gtk_widget_get_toplevel(audio_tabs) : NULL;
   gchar *error = NULL;
   gboolean ok = gtk_pipeline_editor_apply_output(parent, json, &error);
   if (!ok) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
         GTK_BUTTONS_CLOSE, "%s", error ? error : "Cannot apply GStreamer pipeline changes.");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
   }
   if (ok) routing_effect_refresh();
   g_free(error);
   return ok;
}
static gchar *pipeline_config_json(void) {
   dict *document = dict_new();
   if (!document) return NULL;
   const char *key;
   char *value;
   guint count = 0;
   int rank = 0;
   gboolean ok = TRUE;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!g_str_has_prefix(key, "pipeline:")) continue;
      if (count >= 512) { ok = FALSE; break; }
      gchar *id_key = g_strdup_printf("pipelines.%u.id", count);
      gchar *launch_key = g_strdup_printf("pipelines.%u.launch", count);
      ok = !dict_add(document, id_key, key + 9) &&
           !dict_add(document, launch_key, value ? value : "");
      g_free(id_key);
      g_free(launch_key);
      if (!ok) break;
      count++;
   }
   if (ok) ok = !dict_add_uint(document, "pipelines.count", count);
   gchar *json = ok ? dict2json(document) : NULL;
   dict_free(document);
   return json;
}
static void routing_focus_pipeline(const char *id) {
   const char *pipeline = pipeline_for_node(id);
   if (!pipeline || !audio_tabs) return;
#ifdef RR_HAVE_GSTREAMER
   if (pipeline_editor_widget)
      rr_pipeline_editor_select(pipeline_editor_widget, pipeline);
#endif
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
}
static GtkWidget *pipeline_page_new(void) {
#ifndef RR_HAVE_GSTREAMER
   return gtk_label_new("Pipelines require a GStreamer-enabled GTK build.");
#else
   gchar *json = pipeline_config_json();
   GError *error = NULL;
   GtkWidget *editor = json ? rr_pipeline_editor_new(json, pipeline_apply, NULL,
                                                        pipeline_element_focused, NULL, &error) : NULL;
   g_free(json);
   if (!editor) {
      g_warning("Cannot initialize integrated GStreamer editor: %s",
                error ? error->message : "out of memory");
      GtkWidget *label = gtk_label_new(error ? error->message :
         "Unable to initialize the integrated pipeline editor");
      g_clear_error(&error);
      return label;
   }
   pipeline_editor_widget = editor;
   gtk_widget_set_hexpand(editor, TRUE);
   gtk_widget_set_vexpand(editor, TRUE);
   g_object_set_data(G_OBJECT(editor), "rr-scale-exempt", GINT_TO_POINTER(1));
   return editor;
#endif
}

static void routing_lv2_remove_clicked(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   unsigned slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-slot"));
   if (!slot || slot > RR_AUDIO_EFFECT_MAX) return;
   GtkWidget *parent = gtk_widget_get_toplevel(graph_canvas);
   GtkWidget *dialog = gtk_message_dialog_new(
      GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      "Remove LV2 effect %u and stop its subprocess?", slot);
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
                          "_Remove", GTK_RESPONSE_ACCEPT, NULL);
   gboolean remove = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
   gtk_widget_destroy(dialog);
   if (remove) {
      rr_audio_effect_request_t req = {.action = 'D', .slot = slot};
      audio_request_effect_block(&req);
   }
}
static void routing_lv2_disconnect(GtkMenuItem *item, gpointer unused) {
   (void)unused;
   guint slot = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-slot"));
   guint encoded = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "lv2-port"));
   rr_audio_effect_request_t req = {
      .action = encoded >= 2 ? 'O' : 'R',
      .slot = slot,
      .channel = encoded % 2,
      .output = 'M'
   };
   audio_request_effect_block(&req);
   routing_queue();
}
static gboolean routing_lv2_context_menu(GdkEventButton *event, double gx, double gy) {
   if (!graph_snapshot) return FALSE;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      if (!g_str_has_prefix(node->id, "proc.lv2.")) continue;
      double x = routing_node_x(node), y = routing_node_y(node);
      if (gx < x || gx > x + RR_GTK_GRAPH_NODE_W ||
          gy < y || gy > y + RR_GTK_GRAPH_NODE_H) continue;
      unsigned slot = 0;
      if (sscanf(node->id, "proc.lv2.%u", &slot) != 1 ||
          slot < 1 || slot > RR_AUDIO_EFFECT_MAX) return FALSE;
      GtkWidget *menu = gtk_menu_new();
      for (guint ch = 0; ch < (node->channels == 2 ? 2u : 1u); ch++) {
         for (guint direction = 0; direction < 2; direction++) {
            gchar *label = g_strdup_printf("Disconnect %s %s",
               direction ? "output" : "input",
               node->channels == 2 ? (ch ? "R" : "L") : "MONO");
            GtkWidget *disconnect = gtk_menu_item_new_with_label(label);
            g_free(label);
            g_object_set_data(G_OBJECT(disconnect), "lv2-slot", GUINT_TO_POINTER(slot));
            g_object_set_data(G_OBJECT(disconnect), "lv2-port",
                              GUINT_TO_POINTER(direction * 2 + ch));
            g_signal_connect(disconnect, "activate",
                             G_CALLBACK(routing_lv2_disconnect), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), disconnect);
         }
      }
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
      GtkWidget *remove = gtk_menu_item_new_with_label("Remove LV2 Effect…");
      g_object_set_data(G_OBJECT(remove), "lv2-slot", GUINT_TO_POINTER(slot));
      g_signal_connect(remove, "activate",
         G_CALLBACK(routing_lv2_remove_clicked), NULL);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
      g_signal_connect_swapped(menu, "selection-done",
                               G_CALLBACK(gtk_widget_destroy), menu);
      gtk_widget_show_all(menu);
      gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
      return TRUE;
   }
   return FALSE;
}
#ifdef RR_HAVE_GSTREAMER
/* Inspect the installed LV2 factory's real audio pad templates in GTK,
 * where GStreamer is linked. The core only receives the channel count. */
static guint routing_lv2_audio_channels(const char *name) {
   GstElementFactory *factory = gst_element_factory_find(name);
   if (!factory) return 1;
   GstCaps *mono = gst_caps_from_string("audio/x-raw,channels=(int)1");
   GstCaps *stereo = gst_caps_from_string("audio/x-raw,channels=(int)2");
   gboolean in_mono = FALSE, out_mono = FALSE, in_stereo = FALSE, out_stereo = FALSE;
   for (const GList *it = gst_element_factory_get_static_pad_templates(factory);
        it; it = it->next) {
      const GstStaticPadTemplate *pt = it->data;
      GstCaps *caps = gst_static_pad_template_get_caps((GstStaticPadTemplate *)pt);
      if (!caps) continue;
      if (pt->direction == GST_PAD_SINK) {
         in_mono |= gst_caps_can_intersect(caps, mono);
         in_stereo |= gst_caps_can_intersect(caps, stereo);
      } else if (pt->direction == GST_PAD_SRC) {
         out_mono |= gst_caps_can_intersect(caps, mono);
         out_stereo |= gst_caps_can_intersect(caps, stereo);
      }
      gst_caps_unref(caps);
   }
   gboolean stereo_only = in_stereo && out_stereo &&
       (!in_mono || !out_mono);
   gst_caps_unref(mono); gst_caps_unref(stereo);
   gst_object_unref(factory);
   return stereo_only ? 2 : 1;
}
enum { LV2_NAME, LV2_DESC, LV2_LONG, LV2_CLASS, LV2_N };
typedef struct { GtkWidget *search, *details, *dialog; GtkTreeView *view; GtkTreeModelFilter *filter; } lv2_picker_t;
static gboolean lv2_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer data) {
   const char *query = gtk_entry_get_text(GTK_ENTRY(((lv2_picker_t *)data)->search));
   if (!*query) return TRUE;
   gchar *name = NULL, *desc = NULL, *longname = NULL, *klass = NULL;
   gtk_tree_model_get(m, it, LV2_NAME, &name, LV2_DESC, &desc, LV2_LONG, &longname, LV2_CLASS, &klass, -1);
   gchar *q = g_utf8_casefold(query, -1);
   gchar *n = g_utf8_casefold(name ? name : "", -1), *d = g_utf8_casefold(desc ? desc : "", -1);
   gchar *l = g_utf8_casefold(longname ? longname : "", -1), *k = g_utf8_casefold(klass ? klass : "", -1);
   gboolean match = strstr(n, q) || strstr(d, q) || strstr(l, q) || strstr(k, q);
   g_free(q); g_free(n); g_free(d); g_free(l); g_free(k);
   g_free(name); g_free(desc); g_free(longname); g_free(klass);
   return match;
}
static void lv2_selected(GtkTreeSelection *sel, gpointer data) {
   lv2_picker_t *p = data;
   GtkTreeModel *m; GtkTreeIter it;
   if (!gtk_tree_selection_get_selected(sel, &m, &it)) {
      gtk_label_set_text(GTK_LABEL(p->details), "No matching plugins");
      gtk_dialog_set_response_sensitive(GTK_DIALOG(p->dialog), GTK_RESPONSE_ACCEPT, FALSE);
      return;
   }
   gchar *name = NULL, *desc = NULL, *longname = NULL, *klass = NULL;
   gtk_tree_model_get(m, &it, LV2_NAME, &name, LV2_DESC, &desc, LV2_LONG, &longname, LV2_CLASS, &klass, -1);
   guint channels = routing_lv2_audio_channels(name ? name : "");
   gchar *markup = g_markup_printf_escaped(
      "<b>%s</b>\n%s\n\n<b>Factory:</b> %s\n<b>Type:</b> %s\n<b>Audio ports:</b> %s",
      longname && *longname ? longname : name ? name : "",
      desc && *desc ? desc : "No description available", name ? name : "",
      klass && *klass ? klass : "LV2",
      channels == 2 ? "Stereo (L/R input and output)" : "Mono (single input and output)");
   gtk_label_set_markup(GTK_LABEL(p->details), markup);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(p->dialog), GTK_RESPONSE_ACCEPT, TRUE);
   g_free(markup); g_free(name); g_free(desc); g_free(longname); g_free(klass);
}
static void lv2_search(GtkEditable *entry, gpointer data) {
   (void)entry;
   lv2_picker_t *p = data;
   gtk_tree_model_filter_refilter(p->filter);
   GtkTreeIter it; GtkTreeModel *m = GTK_TREE_MODEL(p->filter);
   GtkTreeSelection *sel = gtk_tree_view_get_selection(p->view);
   if (gtk_tree_model_get_iter_first(m, &it)) gtk_tree_selection_select_iter(sel, &it);
   else gtk_tree_selection_unselect_all(sel);
}
static void lv2_activate(GtkTreeView *v, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data) {
   (void)v; (void)path; (void)col;
   gtk_dialog_response(GTK_DIALOG(((lv2_picker_t *)data)->dialog), GTK_RESPONSE_ACCEPT);
}
#endif

static void routing_add_effect(GtkButton *button, gpointer unused) {
   (void)unused;
#ifdef RR_HAVE_GSTREAMER
   if (!gst_is_initialized()) gst_init(NULL, NULL);
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Add LV2 Audio Block",
      GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Add Effect", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid, TRUE, TRUE, 0);
   lv2_picker_t picker = {.dialog = dialog};
   picker.search = gtk_search_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(picker.search), "Search LV2 plugins");
   gtk_grid_attach(GTK_GRID(grid), picker.search, 0, 0, 2, 1);
   GtkListStore *store = gtk_list_store_new(LV2_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
   GList *features = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
   guint found = 0;
   for (GList *it = features; it; it = it->next) {
      GstElementFactory *factory = it->data;
      if (g_strcmp0(gst_plugin_feature_get_plugin_name(GST_PLUGIN_FEATURE(factory)), "lv2")) continue;
      GtkTreeIter row;
      gtk_list_store_append(store, &row);
      gtk_list_store_set(store, &row,
         LV2_NAME, gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),
         LV2_DESC, gst_element_factory_get_description(factory),
         LV2_LONG, gst_element_factory_get_longname(factory),
         LV2_CLASS, gst_element_factory_get_klass(factory), -1);
      found++;
   }
   gst_plugin_feature_list_free(features);
   GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(store));
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sorted), LV2_NAME, GTK_SORT_ASCENDING);
   picker.filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(sorted, NULL));
   gtk_tree_model_filter_set_visible_func(picker.filter, lv2_visible, &picker, NULL);
   picker.view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(picker.filter)));
   gtk_tree_view_set_headers_visible(picker.view, FALSE);
   GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
   g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
   gtk_tree_view_insert_column_with_attributes(picker.view, -1, "Plugin", renderer, "text", LV2_NAME, NULL);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_widget_set_size_request(scroll, 490, 200);
   gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(picker.view));
   gtk_grid_attach(GTK_GRID(grid), scroll, 0, 1, 2, 1);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(picker.view);
   g_signal_connect(selection, "changed", G_CALLBACK(lv2_selected), &picker);
   g_signal_connect(picker.search, "changed", G_CALLBACK(lv2_search), &picker);
   g_signal_connect(picker.view, "row-activated", G_CALLBACK(lv2_activate), &picker);
   picker.details = gtk_label_new(NULL);
   gtk_label_set_xalign(GTK_LABEL(picker.details), 0.);
   gtk_label_set_yalign(GTK_LABEL(picker.details), 0.);
   gtk_label_set_line_wrap(GTK_LABEL(picker.details), TRUE);
   gtk_label_set_line_wrap_mode(GTK_LABEL(picker.details), PANGO_WRAP_WORD_CHAR);
   gtk_label_set_max_width_chars(GTK_LABEL(picker.details), 60);
   GtkWidget *frame = gtk_frame_new("Selected plugin");
   gtk_container_add(GTK_CONTAINER(frame), picker.details);
   gtk_grid_attach(GTK_GRID(grid), frame, 0, 2, 2, 1);
   GtkWidget *source_box = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_box), "", "Unconnected — wire input in Routing");
   gtk_combo_box_set_active(GTK_COMBO_BOX(source_box), 0);
   if (graph_snapshot)
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
         if (node->type != RR_AG_SOURCE || node->read_only ||
             (node->rate_hz && node->rate_hz != 16000) ||
             !strcmp(node->id, "recording.playback")) continue;
         gchar *label = node->rig[0] ?
            g_strdup_printf("%.58s (%s)", node->rig, node->id) :
            g_strdup(node->id);
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_box),
                                    node->id, label);
         g_free(label);
      }
   GtkWidget *source_label = gtk_label_new("RX source");
   gtk_widget_set_halign(source_label, GTK_ALIGN_END);
   gtk_grid_attach(GTK_GRID(grid), source_label, 0, 3, 1, 1);
   gtk_widget_set_hexpand(source_box, TRUE);
   gtk_grid_attach(GTK_GRID(grid), source_box, 1, 3, 1, 1);
   GtkWidget *hint = gtk_label_new(
      "Independent fwdsp subprocess, 16 kHz mono. Leave unconnected to patch input later.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_label_set_max_width_chars(GTK_LABEL(hint), 60);
   gtk_grid_attach(GTK_GRID(grid), hint, 0, 4, 2, 1);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT,
                                     found != 0);
   gtk_widget_show_all(dialog);
   lv2_search(GTK_EDITABLE(picker.search), &picker);
   gtk_widget_grab_focus(picker.search);
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      GtkTreeModel *model = NULL;
      GtkTreeIter chosen;
      gchar *factory = NULL;
      if (gtk_tree_selection_get_selected(selection, &model, &chosen))
         gtk_tree_model_get(model, &chosen, LV2_NAME, &factory, -1);
      const char *source = gtk_combo_box_get_active_id(GTK_COMBO_BOX(source_box));
      if (factory && source) {
         rr_audio_effect_request_t req = {.action = 'A'};
         req.channels = routing_lv2_audio_channels(factory);
         g_strlcpy(req.factory, factory, sizeof(req.factory));
         g_strlcpy(req.source, source, sizeof(req.source));
         audio_request_effect_block(&req);
      }
      g_free(factory);
   }
   g_object_unref(picker.filter);
   g_object_unref(sorted);
   g_object_unref(store);
   gtk_widget_destroy(dialog);
#else
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_message_dialog_new(
      GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
      "LV2 effects require a build with GStreamer support.");
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
#endif
}

/* Small reusable parameter widgets use the same core event path as the
 * graph's own trims. The shared floating-panel manager provides docking,
 * roll-up and saved geometry. Stable IDs identify the target, not its row. */
typedef struct routing_compact_control {
   char id[64], uuid[40];
   char label[32], kind[16], unit[16];
   char effect_worker[64], effect_element[96], effect_property[96];
   double effect_value, effect_default, hint_min, hint_max, hint_step;
   double x, y;
   double minimum, maximum, step;
   gboolean has_minimum, has_maximum, has_step, has_unit, has_label;
   gboolean moving, wiring;
   double start_x, start_y, pointer_x, pointer_y;
   gboolean position_dirty, hidden;
   guint save_source;
   unsigned channel;
   gboolean port;
   gboolean updating, knob_dragging;
   double knob_y, knob_initial;
   RRFloatPanel *panel;
   GtkWidget *value, *knob, *label_widget, *move_handle;
} routing_compact_control_t;
typedef struct {
   char worker[64], element[96], property[96];
   double low, high, step, initial;
} routing_effect_port_t;
static gboolean routing_effect_pin_at(double x, double y, routing_effect_port_t *out);
static gboolean routing_effect_parameter_location(const char *worker,
   const char *element, const char *property, double *x, double *y);
static void routing_compact_set_effect(routing_compact_control_t *control,
                                       const routing_effect_port_t *port);
static GPtrArray *routing_compact_controls;
static gboolean routing_control_drag_wire;
static void routing_compact_sync(void);
static double routing_compact_current(const routing_compact_control_t *control);
static void routing_compact_apply_hints(routing_compact_control_t *control) {
   /* Explicit overrides are retained; absent values use metadata supplied by
    * the active binding. Mixer gain ports currently publish dB/-60/+12/1.
    * Future LV2 bindings can replace this hint provider without changing UI. */
   if (!control->has_minimum) control->minimum =
      control->effect_worker[0] ? control->hint_min : GRAPH_GAIN_MIN;
   if (!control->has_maximum) control->maximum =
      control->effect_worker[0] ? control->hint_max : GRAPH_GAIN_MAX;
   if (!control->has_step) control->step =
      control->effect_worker[0] ? control->hint_step : 1.;
   if (!control->has_unit)
      g_strlcpy(control->unit, control->effect_worker[0] ? "" : "dB",
                sizeof(control->unit));
   if (!control->has_label) {
      if (control->effect_worker[0])
         g_strlcpy(control->label, control->effect_property, sizeof(control->label));
      else
         g_snprintf(control->label, sizeof(control->label), "%s %c gain",
                    control->port ? "Output" : "Input",
                    control->channel ? 'R' : 'L');
   }
   if (!isfinite(control->minimum) || !isfinite(control->maximum) ||
       control->maximum <= control->minimum) {
      control->minimum = GRAPH_GAIN_MIN; control->maximum = GRAPH_GAIN_MAX;
   }
   if (!isfinite(control->step) || control->step <= 0)
      control->step = 1.;
}
static void routing_compact_apply_widget(routing_compact_control_t *control) {
   if (!control->value) return;
   control->updating = TRUE;
   gtk_range_set_range(GTK_RANGE(control->value),
                       control->minimum, control->maximum);
   gtk_range_set_increments(GTK_RANGE(control->value),
                            control->step, control->step * 5.);
   gtk_range_set_value(GTK_RANGE(control->value),
                       CLAMP(routing_compact_current(control),
                             control->minimum, control->maximum));
   gtk_scale_set_digits(GTK_SCALE(control->value),
      control->step < .01 ? 3 : control->step < 1. ? 2 : 0);
   if (control->panel) rr_floating_panel_set_title(control->panel, control->label);
   if (control->label_widget) gtk_label_set_text(GTK_LABEL(control->label_widget), control->label);
   if (control->knob) gtk_widget_queue_draw(control->knob);
   control->updating = FALSE;
}
static double routing_compact_current(const routing_compact_control_t *control) {
   if (control->effect_worker[0]) return control->effect_value;
   if (!graph_snapshot) return 0.;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      if (strcmp(node->id, control->id)) continue;
      return control->port ? node->port_gain_db[control->channel] :
                             node->input_gain_db[control->channel];
   }
   return 0.;
}
static void routing_compact_sync(void) {
   if (!routing_compact_controls) return;
   for (guint i = 0; i < routing_compact_controls->len; i++) {
      routing_compact_control_t *control =
         g_ptr_array_index(routing_compact_controls, i);
      double pin_x = 0., pin_y = 0.;
      gboolean found = control->effect_worker[0] &&
         routing_effect_parameter_location(control->effect_worker,
            control->effect_element, control->effect_property,
            &pin_x, &pin_y);
      if (!found && !control->effect_worker[0] && graph_snapshot)
         for (guint n = 0; n < graph_snapshot->node_count; n++)
            if (!strcmp(graph_snapshot->nodes[n].id, control->id) &&
                !graph_missing_since[n]) {
               found = TRUE;
               break;
            }
      gtk_widget_set_sensitive(control->value, found);
      /* Never overwrite a knob/fader mid-drag. */
      if (found && !control->effect_worker[0] && !control->knob_dragging &&
          !gtk_widget_has_focus(control->value)) {
         control->updating = TRUE;
         gtk_range_set_value(GTK_RANGE(control->value),
                             routing_compact_current(control));
         control->updating = FALSE;
         if (control->knob) gtk_widget_queue_draw(control->knob);
      }
   }
}
static gboolean routing_compact_flush_value(gpointer user) {
   routing_compact_control_t *control = user;
   control->save_source = 0;
   routing_compact_save(control);
   return G_SOURCE_REMOVE;
}
static void routing_compact_changed(GtkRange *range, gpointer user) {
   routing_compact_control_t *control = user;
   if (control->updating || (!control->id[0] && !control->effect_worker[0]))
      return;
   double db = gtk_range_get_value(range);
   if (control->effect_worker[0]) {
      control->effect_value = db;
      if (control->save_source) g_source_remove(control->save_source);
      control->save_source = g_timeout_add(400, routing_compact_flush_value,
                                           control);
      audio_request_effect_parameter(control->effect_worker,
          control->effect_element, control->effect_property, db);
      if (control->knob) gtk_widget_queue_draw(control->knob);
      return;
   }
   if (control->port)
      audio_request_port_gain(control->id, control->channel, db);
   else
      audio_request_rx_input_gain(control->id, control->channel, db);
   if (graph_snapshot) {
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
         if (strcmp(node->id, control->id)) continue;
         if (control->port) node->port_gain_db[control->channel] = db;
         else node->input_gain_db[control->channel] = db;
         break;
      }
   }
   if (control->knob) gtk_widget_queue_draw(control->knob);
   graph_canvas_update_size();
}
static gboolean routing_compact_reset(GtkWidget *widget, GdkEventButton *event,
                                       gpointer unused) {
   (void)unused;
   if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_2BUTTON_PRESS)
      return FALSE;
   routing_compact_control_t *control = g_object_get_data(
      G_OBJECT(widget), "rr-compact-control");
   gtk_range_set_value(GTK_RANGE(widget),
      control && control->effect_worker[0] ? control->effect_default : 0.);
   return TRUE;
}
/* Minimal Cairo knob: vertical drag or wheel controls the same GTK
 * adjustment and thus the exact same asynchronous core request. */
static gboolean routing_knob_draw(GtkWidget *widget, cairo_t *cr, gpointer user) {
   routing_compact_control_t *control = user;
   double width = gtk_widget_get_allocated_width(widget);
   double height = gtk_widget_get_allocated_height(widget);
   double cx = width / 2., cy = height / 2.;
   double radius = MIN(width, height) / 2. - 7.;
   double value = gtk_range_get_value(GTK_RANGE(control->value));
   double fraction = CLAMP((value - control->minimum) /
                            MAX(.00001, control->maximum - control->minimum), 0., 1.);
   double begin = 2.35, end = 2.35 + 4.73;
   cairo_set_line_width(cr, 5.);
   cairo_set_source_rgb(cr, .30, .32, .34);
   cairo_arc(cr, cx, cy, radius, begin, end);
   cairo_stroke(cr);
   cairo_set_source_rgb(cr, .23, .72, .92);
   cairo_arc(cr, cx, cy, radius, begin, begin + 4.73 * fraction);
   cairo_stroke(cr);
   double angle = begin + 4.73 * fraction;
   cairo_set_source_rgb(cr, .94, .94, .94);
   cairo_set_line_width(cr, 2.5);
   cairo_move_to(cr, cx, cy);
   cairo_line_to(cr, cx + (radius - 4.) * cos(angle),
                    cy + (radius - 4.) * sin(angle));
   cairo_stroke(cr);
   char text[32];
   g_snprintf(text, sizeof(text), "%.*f %s", control->step < 1. ? 2 : 0,
              value, control->unit);
   cairo_set_font_size(cr, 10.);
   cairo_text_extents_t ext;
   cairo_text_extents(cr, text, &ext);
   cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy + radius + 5.);
   cairo_show_text(cr, text);
   return FALSE;
}
static gboolean routing_knob_press(GtkWidget *widget, GdkEventButton *event,
                                   gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   if (event->type == GDK_2BUTTON_PRESS) {
      gtk_range_set_value(GTK_RANGE(control->value),
          control->effect_worker[0] ? control->effect_default : 0.);
      return TRUE;
   }
   control->knob_dragging = TRUE;
   control->knob_y = event->y_root;
   control->knob_initial = gtk_range_get_value(GTK_RANGE(control->value));
   gtk_widget_grab_focus(widget);
   return TRUE;
}
static gboolean routing_knob_motion(GtkWidget *widget, GdkEventMotion *event,
                                    gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;
   if (!control->knob_dragging) return FALSE;
   gtk_range_set_value(GTK_RANGE(control->value),
      CLAMP(control->knob_initial + (control->knob_y - event->y_root) *
              (control->maximum - control->minimum) / 144.,
            control->minimum, control->maximum));
   return TRUE;
}
static gboolean routing_knob_release(GtkWidget *widget, GdkEventButton *event,
                                     gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   control->knob_dragging = FALSE;
   return TRUE;
}
static gboolean routing_knob_scroll(GtkWidget *widget, GdkEventScroll *event,
                                    gpointer user) {
   (void)widget;
   routing_compact_control_t *control = user;
   double delta = event->direction == GDK_SCROLL_UP ? 1. :
                  event->direction == GDK_SCROLL_DOWN ? -1. : 0.;
   if (!delta) return FALSE;
   gtk_range_set_value(GTK_RANGE(control->value),
       CLAMP(gtk_range_get_value(GTK_RANGE(control->value)) +
             delta * control->step, control->minimum, control->maximum));
   return TRUE;
}
static void routing_compact_destroy(gpointer pointer) {
   routing_compact_control_t *control = pointer;
   if (control->save_source) g_source_remove(control->save_source);
   if (control->panel) rr_floating_panel_destroy(control->panel);
   g_free(control);
}
/* Popup menu actions refer to an immutable UUID; deletion occurs from
 * an idle callback so no GtkMenu handler destroys its own event source. */
static void routing_compact_set_hidden(routing_compact_control_t *control, gboolean hide) {
   if (!control || !control->panel) return;
   control->hidden = hide;
   if (hide) rr_floating_panel_hide(control->panel);
   else rr_floating_panel_present(control->panel);
   routing_compact_save(control);
}
static gboolean routing_compact_delete_idle(gpointer data) {
   char *uuid = data;
   if (routing_compact_controls) {
      for (guint i = 0; i < routing_compact_controls->len; i++) {
         routing_compact_control_t *control =
            g_ptr_array_index(routing_compact_controls, i);
         if (strcmp(control->uuid, uuid)) continue;
         g_ptr_array_remove_index(routing_compact_controls, i);
         if (routing_compact_controls->len)
            routing_compact_save(g_ptr_array_index(routing_compact_controls, 0));
         else
            rrclient_database_setting_set("audio.controls.ids", "");
         graph_canvas_update_size();
         break;
      }
   }
   g_free(uuid);
   return G_SOURCE_REMOVE;
}
static void routing_compact_menu_action(GtkMenuItem *item, gpointer data) {
   routing_compact_control_t *control = data;
   const char *action = g_object_get_data(G_OBJECT(item), "control-action");
   if (!g_strcmp0(action, "hide"))
      routing_compact_set_hidden(control, TRUE);
   else if (!g_strcmp0(action, "delete"))
      g_idle_add(routing_compact_delete_idle, g_strdup(control->uuid));
   else if (!g_strcmp0(action, "move")) {
      gtk_widget_set_tooltip_text(control->move_handle,
         "Drag this label to reposition the control");
      GdkWindow *window = gtk_widget_get_window(control->move_handle);
      if (window) {
         GdkDisplay *display = gdk_window_get_display(window);
         GdkCursor *cursor = gdk_cursor_new_from_name(display, "move");
         if (cursor) { gdk_window_set_cursor(window, cursor); g_object_unref(cursor); }
      }
   }
}
static gboolean routing_compact_context(GtkWidget *widget,
                                        GdkEventButton *event, gpointer data) {
   (void)widget;
   routing_compact_control_t *control = data;
   if (event->button != GDK_BUTTON_SECONDARY) return FALSE;
   GtkWidget *menu = gtk_menu_new();
   const char *items[] = {"Move (drag label)", "Hide", "Delete"};
   const char *actions[] = {"move", "hide", "delete"};
   for (guint i = 0; i < G_N_ELEMENTS(items); i++) {
      GtkWidget *entry = gtk_menu_item_new_with_label(items[i]);
      g_object_set_data(G_OBJECT(entry), "control-action", (gpointer)actions[i]);
      g_signal_connect(entry, "activate",
         G_CALLBACK(routing_compact_menu_action), control);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
   }
   g_signal_connect_swapped(menu, "selection-done",
      G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
   return TRUE;
}
static void routing_compact_restore_one(routing_compact_control_t *control);
static void routing_compact_hidden_menu(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *menu = gtk_menu_new();
   guint found = 0;
   if (routing_compact_controls)
      for (guint i = 0; i < routing_compact_controls->len; i++) {
         routing_compact_control_t *control =
            g_ptr_array_index(routing_compact_controls, i);
         if (!control->hidden) continue;
         GtkWidget *entry = gtk_menu_item_new_with_label(control->label);
         g_signal_connect_swapped(entry, "activate",
            G_CALLBACK(routing_compact_restore_one), control);
         gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
         found++;
      }
   if (!found) {
      GtkWidget *empty = gtk_menu_item_new_with_label("No hidden controls");
      gtk_widget_set_sensitive(empty, FALSE);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty);
   }
   g_signal_connect_swapped(menu, "selection-done",
      G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);
   gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(button),
       GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}
static void routing_compact_restore_one(routing_compact_control_t *control) {
   routing_compact_set_hidden(control, FALSE);
}
static void routing_compact_add(const char *node_id, unsigned channel, gboolean port,
                                const char *label, const char *kind,
                                const char *saved_uuid, double saved_x, double saved_y) {
   if (!routing_overlay || !GTK_IS_NOTEBOOK(main_notebook)) return;
   if (!routing_compact_controls)
      routing_compact_controls = g_ptr_array_new_with_free_func(routing_compact_destroy);
   routing_compact_control_t *control = g_new0(routing_compact_control_t, 1);
   char *uuid = saved_uuid ? NULL : g_uuid_string_random();
   g_strlcpy(control->uuid, saved_uuid ? saved_uuid : uuid, sizeof(control->uuid));
   g_free(uuid);
   g_strlcpy(control->id, node_id ? node_id : "", sizeof(control->id));
   if (label && *label) {
      g_strlcpy(control->label, label, sizeof(control->label));
      control->has_label = TRUE;
   }
   g_strlcpy(control->kind, kind ? kind : "horizontal", sizeof(control->kind));
   routing_compact_apply_hints(control);
   /* Distribute newly created graph controls rather than stacking all
    * unbound/LV2 controls at the same point. Saved positions win below. */
   guint ordinal = routing_compact_controls->len;
   control->x = GRAPH_LAYOUT_MARGIN + 15. + (ordinal % 4) * 135.;
   control->y = GRAPH_LAYOUT_MARGIN + 160. + (ordinal / 4) * 82.;
   if (node_id && *node_id && graph_snapshot) for (guint i = 0; i < graph_snapshot->node_count; i++)
      if (!strcmp(graph_snapshot->nodes[i].id, node_id)) {
         control->x = MAX(0., routing_node_x(&graph_snapshot->nodes[i]) - 135.);
         control->y = routing_node_y(&graph_snapshot->nodes[i]) + channel * 36.;
         break;
      }
   control->channel = channel;
   control->port = port;
   if (saved_uuid) { control->x = CLAMP(saved_x, 0., 100000.);
                     control->y = CLAMP(saved_y, 0., 100000.); }
   char *panel_id = g_strdup_printf("audio-compact-%s", control->uuid);
   control->panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay),
      GTK_NOTEBOOK(main_notebook), panel_id, control->label);
   g_free(panel_id);
   if (!control->panel) { g_free(control); return; }
   /* The preferred location is only used for new panels or for repairing
    * overlapping legacy window_states. A distinct saved position wins. */
   gint overlay_width = gtk_widget_get_allocated_width(routing_overlay);
   gint columns = overlay_width > 300 ?
      MAX(1, MIN(4, (overlay_width - 24) / 155)) : 4;
   rr_floating_panel_set_initial_position(control->panel,
      18 + (gint)(ordinal % (guint)columns) * 155,
      85 + (gint)(ordinal / (guint)columns) * 125);
   rr_floating_panel_set_resizable(control->panel, FALSE);
   rr_floating_panel_set_minimizable(control->panel, FALSE);
   GtkWidget *body = rr_floating_panel_body(control->panel);
   gtk_box_set_spacing(GTK_BOX(body), 0);
   GtkWidget *handle = gtk_event_box_new();
   gtk_event_box_set_visible_window(GTK_EVENT_BOX(handle), FALSE);
   GtkWidget *title = gtk_label_new(control->label);
   control->label_widget = title;
   control->move_handle = handle;
   gtk_widget_set_tooltip_text(handle, "Drag label to move; right-click for menu");
   gtk_container_add(GTK_CONTAINER(handle), title);
   gtk_box_pack_start(GTK_BOX(body), handle, FALSE, FALSE, 0);
   rr_floating_panel_set_compact(control->panel, handle);
   g_signal_connect(handle, "button-press-event",
      G_CALLBACK(routing_compact_context), control);

   gboolean vertical = !g_strcmp0(kind, "vertical");
   gboolean knob = !g_strcmp0(kind, "knob");
   GtkWidget *scale = gtk_scale_new_with_range(
      vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL,
      control->minimum, control->maximum, control->step);
   control->value = scale;
   g_object_set_data(G_OBJECT(scale), "rr-compact-control", control);
   gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
   gtk_scale_set_digits(GTK_SCALE(scale), 0);
   gtk_widget_set_size_request(scale, vertical ? 34 : 96, vertical ? 90 : 20);
   gtk_widget_set_tooltip_text(scale,
      "Double-click to reset to the parameter default (0 dB for gains)");
   control->updating = TRUE;
   gtk_range_set_value(GTK_RANGE(scale), routing_compact_current(control));
   control->updating = FALSE;
   g_signal_connect(scale, "value-changed", G_CALLBACK(routing_compact_changed), control);
   gtk_widget_add_events(scale, GDK_BUTTON_PRESS_MASK);
   g_signal_connect(scale, "button-press-event", G_CALLBACK(routing_compact_reset), NULL);
   g_signal_connect(scale, "button-press-event", G_CALLBACK(routing_compact_context), control);
   gtk_box_pack_start(GTK_BOX(body), scale, FALSE, FALSE, 0);
   routing_compact_apply_widget(control);
   if (knob) {
      gtk_widget_set_no_show_all(scale, TRUE);
      gtk_widget_hide(scale);
      control->knob = gtk_drawing_area_new();
      gtk_widget_set_size_request(control->knob, 62, 68);
      gtk_widget_set_can_focus(control->knob, TRUE);
      gtk_widget_add_events(control->knob, GDK_BUTTON_PRESS_MASK |
         GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
      g_signal_connect(control->knob, "draw", G_CALLBACK(routing_knob_draw), control);
      g_signal_connect(control->knob, "button-press-event",
                       G_CALLBACK(routing_knob_press), control);
      g_signal_connect(control->knob, "button-press-event",
                       G_CALLBACK(routing_compact_context), control);
      g_signal_connect(control->knob, "motion-notify-event",
                       G_CALLBACK(routing_knob_motion), control);
      g_signal_connect(control->knob, "button-release-event",
                       G_CALLBACK(routing_knob_release), control);
      g_signal_connect(control->knob, "scroll-event",
                       G_CALLBACK(routing_knob_scroll), control);
      gtk_box_pack_start(GTK_BOX(body), control->knob, FALSE, FALSE, 0);
   }
   g_ptr_array_add(routing_compact_controls, control);
   if (!saved_uuid) routing_compact_save(control);
   graph_canvas_update_size();
   gtk_widget_show_all(body);
   gtk_widget_set_sensitive(control->value,
      !!control->id[0] || !!control->effect_worker[0]);
   rr_floating_panel_fit_content(control->panel);
   if (saved_uuid)
      rr_floating_panel_show(control->panel); /* Restore saved size, location, roll/minimize. */
   else
      rr_floating_panel_present(control->panel);
}
/* Save explicit overrides separately from inferred binding metadata. The
 * existing floating-panel manager stores independent panel coordinates. */
static void routing_compact_save(const routing_compact_control_t *control) {
   if (!control || !routing_compact_controls) return;
   GString *ids = g_string_new(NULL);
   for (guint i = 0; i < routing_compact_controls->len; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      if (i) g_string_append_c(ids, ',');
      g_string_append(ids, item->uuid);
   }
   rrclient_database_setting_set("audio.controls.ids", ids->str);
   g_string_free(ids, TRUE);
   const char *fields[] = {"target", "label", "kind", "port", "channel",
                            "x", "y", "min", "max", "step", "unit",
                            "worker", "element", "property", "hint-min",
                            "hint-max", "hint-step", "value", "default", "hidden"};
   char port[4], channel[4], x[G_ASCII_DTOSTR_BUF_SIZE], y[G_ASCII_DTOSTR_BUF_SIZE];
   char min[G_ASCII_DTOSTR_BUF_SIZE], max[G_ASCII_DTOSTR_BUF_SIZE], step[G_ASCII_DTOSTR_BUF_SIZE];
   g_snprintf(port, sizeof(port), "%d", control->port ? 1 : 0);
   g_snprintf(channel, sizeof(channel), "%u", control->channel);
   g_ascii_formatd(x, sizeof(x), "%.3f", control->x);
   g_ascii_formatd(y, sizeof(y), "%.3f", control->y);
   g_ascii_formatd(min, sizeof(min), "%.8g", control->minimum);
   g_ascii_formatd(max, sizeof(max), "%.8g", control->maximum);
   g_ascii_formatd(step, sizeof(step), "%.8g", control->step);
   char hmin[G_ASCII_DTOSTR_BUF_SIZE], hmax[G_ASCII_DTOSTR_BUF_SIZE],
        hstep[G_ASCII_DTOSTR_BUF_SIZE], current[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(hmin, sizeof(hmin), "%.8g", control->hint_min);
   g_ascii_formatd(hmax, sizeof(hmax), "%.8g", control->hint_max);
   g_ascii_formatd(hstep, sizeof(hstep), "%.8g", control->hint_step);
   g_ascii_formatd(current, sizeof(current), "%.8g", control->effect_value);
   char default_text[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(default_text, sizeof(default_text), "%.8g", control->effect_default);
   const char *hidden = control->hidden ? "true" : "false";
   const char *values[] = {control->id,
      control->has_label ? control->label : "", control->kind, port, channel,
      x, y, control->has_minimum ? min : "", control->has_maximum ? max : "",
      control->has_step ? step : "", control->has_unit ? control->unit : "",
      control->effect_worker, control->effect_element, control->effect_property,
      hmin, hmax, hstep, current, default_text, hidden};
   for (guint i = 0; i < G_N_ELEMENTS(fields); i++) {
      char *key = g_strdup_printf("audio.control.%s.%s", control->uuid, fields[i]);
      rrclient_database_setting_set(key, values[i]);
      g_free(key);
   }
}
static char *routing_compact_field(const char *uuid, const char *field) {
   char *key = g_strdup_printf("audio.control.%s.%s", uuid, field);
   char *value = rrclient_database_setting_get(key);
   g_free(key);
   return value;
}
static gboolean routing_compact_parse_number(const char *input, double *result) {
   if (!input || !*input) return FALSE;
   char *end = NULL;
   double v = g_ascii_strtod(input, &end);
   if (end == input || *end || !isfinite(v)) return FALSE;
   *result = v;
   return TRUE;
}
static void routing_compact_restore(void) {
   if (routing_compact_controls) return;
   char *ids = rrclient_database_setting_get("audio.controls.ids");
   if (!ids || !*ids) { g_free(ids); return; }
   gchar **parts = g_strsplit(ids, ",", 128);
   for (guint i = 0; parts[i]; i++) {
      if (!g_uuid_string_is_valid(parts[i])) continue;
      char *target = routing_compact_field(parts[i], "target");
      char *label = routing_compact_field(parts[i], "label");
      char *kind = routing_compact_field(parts[i], "kind");
      char *port = routing_compact_field(parts[i], "port");
      char *channel = routing_compact_field(parts[i], "channel");
      char *x = routing_compact_field(parts[i], "x");
      char *y = routing_compact_field(parts[i], "y");
      char *minimum = routing_compact_field(parts[i], "min");
      char *maximum = routing_compact_field(parts[i], "max");
      char *step = routing_compact_field(parts[i], "step");
      char *unit = routing_compact_field(parts[i], "unit");
      char *worker = routing_compact_field(parts[i], "worker");
      char *element = routing_compact_field(parts[i], "element");
      char *property = routing_compact_field(parts[i], "property");
      char *hmin = routing_compact_field(parts[i], "hint-min");
      char *hmax = routing_compact_field(parts[i], "hint-max");
      char *hstep = routing_compact_field(parts[i], "hint-step");
      char *value = routing_compact_field(parts[i], "value");
      char *default_text = routing_compact_field(parts[i], "default");
      char *hidden = routing_compact_field(parts[i], "hidden");
      double px = 0., py = 0., mn = 0., mx = 0., st = 0.;
      gboolean valid = target && strlen(target) < 64 &&
         kind && (!strcmp(kind, "horizontal") || !strcmp(kind, "vertical") ||
                  !strcmp(kind, "knob")) &&
         port && (!strcmp(port, "0") || !strcmp(port, "1")) &&
         channel && (!strcmp(channel, "0") || !strcmp(channel, "1")) &&
         routing_compact_parse_number(x, &px) &&
         routing_compact_parse_number(y, &py) &&
         px >= 0 && py >= 0 && px <= 100000 && py <= 100000;
      if (valid) {
         guint before = routing_compact_controls ? routing_compact_controls->len : 0;
         routing_compact_add(target, channel[0] - '0', port[0] == '1',
                             label, kind, parts[i], px, py);
         if (routing_compact_controls && routing_compact_controls->len > before) {
            routing_compact_control_t *item =
               g_ptr_array_index(routing_compact_controls, routing_compact_controls->len - 1);
            gboolean has_mn = routing_compact_parse_number(minimum, &mn);
            gboolean has_mx = routing_compact_parse_number(maximum, &mx);
            gboolean has_st = routing_compact_parse_number(step, &st);
            if (has_mn && has_mx && mx > mn) {
               item->has_minimum = item->has_maximum = TRUE;
               item->minimum = mn; item->maximum = mx;
            }
            if (has_st && st > 0) { item->has_step = TRUE; item->step = st; }
            if (unit && *unit) {
               g_strlcpy(item->unit, unit, sizeof(item->unit));
               item->has_unit = TRUE;
            }
            if (worker && *worker && element && *element &&
                property && *property && strlen(worker) < sizeof(item->effect_worker) &&
                strlen(element) < sizeof(item->effect_element) &&
                strlen(property) < sizeof(item->effect_property)) {
               g_strlcpy(item->effect_worker, worker, sizeof(item->effect_worker));
               g_strlcpy(item->effect_element, element, sizeof(item->effect_element));
               g_strlcpy(item->effect_property, property, sizeof(item->effect_property));
               item->hint_min = GRAPH_GAIN_MIN;
               item->hint_max = GRAPH_GAIN_MAX;
               item->hint_step = 1.;
               routing_compact_parse_number(hmin, &item->hint_min);
               routing_compact_parse_number(hmax, &item->hint_max);
               routing_compact_parse_number(hstep, &item->hint_step);
               routing_compact_parse_number(value, &item->effect_value);
               item->effect_default = item->effect_value;
               routing_compact_parse_number(default_text, &item->effect_default);
            }
            routing_compact_apply_hints(item);
            routing_compact_apply_widget(item);
            if (!g_strcmp0(hidden, "true")) {
               item->hidden = TRUE;
               rr_floating_panel_hide(item->panel);
            }
         }
      }
      g_free(target); g_free(label); g_free(kind); g_free(port); g_free(channel);
      g_free(x); g_free(y); g_free(minimum); g_free(maximum); g_free(step); g_free(unit);
      g_free(worker); g_free(element); g_free(property);
      g_free(hmin); g_free(hmax); g_free(hstep); g_free(value);
      g_free(default_text); g_free(hidden);
   }
   g_strfreev(parts);
   g_free(ids);
   routing_compact_sync();
}
static void routing_show_controls_toggled(GtkToggleButton *toggle, gpointer unused) {
   (void)unused;
   routing_controls_visible = gtk_toggle_button_get_active(toggle);
   rrclient_database_setting_set("audio.graph.show-controls",
      routing_controls_visible ? "true" : "false");
   /* Presentation-only: no control bindings, gain, routes, or floating
    * panels are touched by this toggle. */
   graph_canvas_update_size();
}
/* Controls are neutral diamonds, not PCM nodes. Green sockets and
 * dashed green wires identify parameter signals exclusively. */
#define ROUTING_CONTROL_WIDTH 112.
#define ROUTING_CONTROL_HEIGHT 58.
static void routing_compact_bounds(double *width, double *height) {
   if (!routing_compact_controls) return;
   for (guint i = 0; i < routing_compact_controls->len; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      *width = MAX(*width, item->x + ROUTING_CONTROL_WIDTH + GRAPH_LAYOUT_MARGIN);
      *height = MAX(*height, item->y + ROUTING_CONTROL_HEIGHT + GRAPH_LAYOUT_MARGIN);
   }
}
static gboolean routing_compact_target(const routing_compact_control_t *item,
                                        double *x, double *y) {
   if (item->effect_worker[0])
      return routing_effect_parameter_location(item->effect_worker,
         item->effect_element, item->effect_property, x, y);
   if (!graph_snapshot) return FALSE;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      if (strcmp(node->id, item->id) || graph_missing_since[i] ||
          !routing_gain_supported(node)) continue;
      /* The target socket is painted over the L/R trim's actual slider.
       * Keep that control pin visually separate from the PCM connector. */
      *x = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      *y = routing_node_y(node) + (item->channel ? 79. : 60.);
      return TRUE;
   }
   return FALSE;
}
static void routing_compact_output_corner(const routing_compact_control_t *item,
   double tx, double ty, double *out_x, double *out_y) {
   const double corners[4][2] = {
      {item->x, item->y + ROUTING_CONTROL_HEIGHT / 2.},
      {item->x + ROUTING_CONTROL_WIDTH / 2., item->y},
      {item->x + ROUTING_CONTROL_WIDTH, item->y + ROUTING_CONTROL_HEIGHT / 2.},
      {item->x + ROUTING_CONTROL_WIDTH / 2., item->y + ROUTING_CONTROL_HEIGHT}
   };
   double best = G_MAXDOUBLE; guint nearest = 2;
   for (guint k = 0; k < G_N_ELEMENTS(corners); k++) {
      double dx = tx - corners[k][0], dy = ty - corners[k][1];
      double distance = dx * dx + dy * dy;
      if (distance < best) { best = distance; nearest = k; }
   }
   *out_x = corners[nearest][0]; *out_y = corners[nearest][1];
}
static void routing_compact_draw(cairo_t *cr) {
   if (!routing_controls_visible || !routing_compact_controls) return;
   for (guint i = 0; i < routing_compact_controls->len; i++) {
      const routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      const double cx = item->x + ROUTING_CONTROL_WIDTH / 2.;
      const double cy = item->y + ROUTING_CONTROL_HEIGHT / 2.;
      double tx, ty;
      gboolean target = routing_compact_target(item, &tx, &ty);
      if (routing_control_drag_index == (gint)i && routing_control_drag_wire) {
         tx = item->pointer_x; ty = item->pointer_y;
         target = TRUE;
      }
      double start_x, start_y;
      routing_compact_output_corner(item,
         target ? tx : item->x + ROUTING_CONTROL_WIDTH + 40.,
         target ? ty : cy, &start_x, &start_y);
      if (target) {
         cairo_set_source_rgba(cr, .24, .84, .38, .90);
         cairo_set_line_width(cr, 2.2);
         const double dash[] = {5., 4.};
         cairo_set_dash(cr, dash, 2, 0.);
         double mx = (start_x + tx) / 2., my = (start_y + ty) / 2.;
         cairo_move_to(cr, start_x, start_y);
         if (fabs(tx - start_x) >= fabs(ty - start_y))
            cairo_curve_to(cr, mx, start_y, mx, ty, tx, ty);
         else
            cairo_curve_to(cr, start_x, my, tx, my, tx, ty);
         cairo_stroke(cr);
         cairo_set_dash(cr, NULL, 0, 0);
         cairo_arc(cr, tx, ty, 5.5, 0, 2 * G_PI);
         cairo_set_line_width(cr, 2.);
         cairo_stroke(cr);
      }
      cairo_new_path(cr);
      cairo_move_to(cr, item->x, cy);
      cairo_line_to(cr, cx, item->y);
      cairo_line_to(cr, item->x + ROUTING_CONTROL_WIDTH, cy);
      cairo_line_to(cr, cx, item->y + ROUTING_CONTROL_HEIGHT);
      cairo_close_path(cr);
      /* Distinct diamond silhouette, neutral body. Green is reserved
       * for control sockets/wiring, like the other port colors. */
      cairo_set_source_rgba(cr, .23, .24, .27, .96);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, .60, .63, .68);
      cairo_set_line_width(cr, 1.7);
      cairo_stroke(cr);
      cairo_arc(cr, start_x, start_y, 5., 0, 2 * G_PI);
      cairo_set_source_rgb(cr, .24, .84, .38);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, .94, .97, 1.);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 10.);
      cairo_text_extents_t ext;
      cairo_text_extents(cr, item->label, &ext);
      cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy - 1.);
      cairo_show_text(cr, item->label);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 8.);
      const char *subtitle = item->kind[0] ? item->kind : "control";
      cairo_text_extents(cr, subtitle, &ext);
      cairo_move_to(cr, cx - ext.width / 2. - ext.x_bearing, cy + 12.);
      cairo_show_text(cr, subtitle);
   }
}
static void routing_compact_properties(routing_compact_control_t *item) {
   GtkWidget *owner = gtk_widget_get_toplevel(graph_canvas);
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Control Properties",
      GTK_IS_WINDOW(owner) ? GTK_WINDOW(owner) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid, TRUE, TRUE, 0);
   GtkWidget *fields[5];
   const char *captions[] = {"Label", "Minimum", "Maximum", "Step", "Unit"};
   char min[G_ASCII_DTOSTR_BUF_SIZE], max[G_ASCII_DTOSTR_BUF_SIZE];
   char step[G_ASCII_DTOSTR_BUF_SIZE];
   g_ascii_formatd(min, sizeof(min), "%g", item->minimum);
   g_ascii_formatd(max, sizeof(max), "%g", item->maximum);
   g_ascii_formatd(step, sizeof(step), "%g", item->step);
   const char *values[] = {item->has_label ? item->label : "",
      item->has_minimum ? min : "", item->has_maximum ? max : "",
      item->has_step ? step : "", item->has_unit ? item->unit : ""};
   for (unsigned i = 0; i < 5; i++) {
      GtkWidget *label = gtk_label_new(captions[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      fields[i] = gtk_entry_new();
      gtk_entry_set_max_length(GTK_ENTRY(fields[i]), i == 0 ? 31 : i == 4 ? 15 : 24);
      gtk_entry_set_text(GTK_ENTRY(fields[i]), values[i]);
      gtk_entry_set_placeholder_text(GTK_ENTRY(fields[i]),
         i == 0 ? item->label : i == 1 ? min : i == 2 ? max : i == 3 ? step : item->unit);
      gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
   }
   GtkWidget *hint = gtk_label_new(
      "Leave a property blank to inherit from the bound parameter. "
      "Gain pins currently support -60…+12 dB; LV2 port metadata will supply its own ranges.");
   gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
   gtk_grid_attach(GTK_GRID(grid), hint, 0, 5, 2, 1);
   gtk_widget_show_all(dialog);
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *name = gtk_entry_get_text(GTK_ENTRY(fields[0]));
      const char *unit = gtk_entry_get_text(GTK_ENTRY(fields[4]));
      const char *str_min = gtk_entry_get_text(GTK_ENTRY(fields[1]));
      const char *str_max = gtk_entry_get_text(GTK_ENTRY(fields[2]));
      const char *str_step = gtk_entry_get_text(GTK_ENTRY(fields[3]));
      double allowed_min = item->effect_worker[0] ? item->hint_min : GRAPH_GAIN_MIN;
      double allowed_max = item->effect_worker[0] ? item->hint_max : GRAPH_GAIN_MAX;
      double mn = allowed_min, mx = allowed_max, st = item->step;
      gboolean hm = routing_compact_parse_number(str_min, &mn);
      gboolean hx = routing_compact_parse_number(str_max, &mx);
      gboolean hs = routing_compact_parse_number(str_step, &st);
      gboolean valid = (!*str_min || hm) && (!*str_max || hx) &&
                       (!*str_step || hs) && st > 0 && mn < mx &&
                       mn >= allowed_min && mx <= allowed_max;
      if (!valid) {
         GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(dialog),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Use finite numbers with min < max, a positive step and "
            "limits within the target's supported range.");
         gtk_dialog_run(GTK_DIALOG(error));
         gtk_widget_destroy(error);
         continue;
      }
      item->has_label = *name != 0;
      if (item->has_label) g_strlcpy(item->label, name, sizeof(item->label));
      item->has_unit = *unit != 0;
      if (item->has_unit) g_strlcpy(item->unit, unit, sizeof(item->unit));
      item->has_minimum = hm;
      item->has_maximum = hx;
      item->has_step = hs;
      item->minimum = mn; item->maximum = mx; item->step = st;
      routing_compact_apply_hints(item);
      routing_compact_apply_widget(item);
      routing_compact_save(item);
      graph_canvas_update_size();
      break;
   }
   gtk_widget_destroy(dialog);
}
static gboolean routing_compact_hit(double x, double y, gboolean dbl) {
   if (!routing_controls_visible || !routing_compact_controls) return FALSE;
   for (gint i = (gint)routing_compact_controls->len - 1; i >= 0; i--) {
      routing_compact_control_t *item = g_ptr_array_index(routing_compact_controls, i);
      double cy = item->y + ROUTING_CONTROL_HEIGHT / 2.;
      double cx = item->x + ROUTING_CONTROL_WIDTH / 2.;
      /* Diamond hit region, including a small radius around the green pin. */
      if (fabs(x - cx) / (ROUTING_CONTROL_WIDTH / 2.) +
          fabs(y - cy) / (ROUTING_CONTROL_HEIGHT / 2.) > 1.15) continue;
      if (dbl) {
         routing_compact_properties(item);
         return TRUE;
      }
      routing_control_drag_index = i;
      double tx, ty, px, py;
      gboolean bound = routing_compact_target(item, &tx, &ty);
      routing_compact_output_corner(item,
         bound ? tx : item->x + ROUTING_CONTROL_WIDTH + 40.,
         bound ? ty : cy, &px, &py);
      routing_control_drag_wire = hypot(x - px, y - py) <= 16.;
      item->start_x = x - item->x;
      item->start_y = y - item->y;
      item->pointer_x = x; item->pointer_y = y;
      item->position_dirty = FALSE;
      return TRUE;
   }
   return FALSE;
}
static gboolean routing_compact_drag_motion(double x, double y) {
   if (!routing_compact_controls || routing_control_drag_index < 0 ||
       (guint)routing_control_drag_index >= routing_compact_controls->len)
      return FALSE;
   routing_compact_control_t *item =
      g_ptr_array_index(routing_compact_controls, routing_control_drag_index);
   if (routing_control_drag_wire) {
      item->pointer_x = x;
      item->pointer_y = y;
   } else {
      double nx = CLAMP(x - item->start_x, 0., 100000.);
      double ny = CLAMP(y - item->start_y, 0., 100000.);
      if (fabs(nx - item->x) > .1 || fabs(ny - item->y) > .1)
         item->position_dirty = TRUE;
      item->x = nx; item->y = ny;
   }
   graph_canvas_update_size();
   return TRUE;
}
static gboolean routing_compact_drag_release(double x, double y) {
   if (!routing_compact_controls || routing_control_drag_index < 0 ||
       (guint)routing_control_drag_index >= routing_compact_controls->len)
      return FALSE;
   routing_compact_control_t *item =
      g_ptr_array_index(routing_compact_controls, routing_control_drag_index);
   if (routing_control_drag_wire) {
      routing_effect_port_t effect = {0};
      if (routing_effect_pin_at(x, y, &effect)) {
         routing_compact_set_effect(item, &effect);
         routing_control_drag_index = -1;
         routing_control_drag_wire = FALSE;
         graph_canvas_update_size();
         return TRUE;
      }
      gint index = -1, channel = -1;
      gboolean port = FALSE;
      if (graph_gain_at(x, y, &index, &channel, &port) &&
          index >= 0 && graph_snapshot && channel >= 0) {
         const rr_ag_view_node_t *target = &graph_snapshot->nodes[index];
         g_strlcpy(item->id, target->id, sizeof(item->id));
         item->effect_worker[0] = item->effect_element[0] = item->effect_property[0] = 0;
         item->channel = (unsigned)channel;
         item->port = port;
         routing_compact_apply_hints(item);
         routing_compact_apply_widget(item);
         routing_compact_save(item);
      }
   } else if (item->position_dirty) {
      routing_compact_save(item);
   }
   routing_control_drag_index = -1;
   routing_control_drag_wire = FALSE;
   graph_canvas_update_size();
   return TRUE;
}
/* Inspect the exact configured client pipeline, never instantiate effects
 * on the GTK audio path. This object is not PLAYING and is discarded after
 * collecting metadata; the live instance belongs to the fwdsp subprocess. */

#ifdef RR_HAVE_GSTREAMER
/* Discover numeric LV2 control metadata without ever starting the plugin
 * in GTK. Both embedded and standalone fwdsp workers use this catalog. */
static void routing_lv2_properties(GtkComboBoxText *choice, GHashTable *ports,
                                   GstElement *element, const char *worker) {
   guint count = 0;
   GParamSpec **specs = g_object_class_list_properties(
       G_OBJECT_GET_CLASS(element), &count);
   for (guint j = 0; j < count; j++) {
      GParamSpec *spec = specs[j];
      if (!(spec->flags & G_PARAM_WRITABLE) ||
          (spec->flags & G_PARAM_CONSTRUCT_ONLY)) continue;
      routing_effect_port_t meta = {0};
      g_strlcpy(meta.worker, worker, sizeof(meta.worker));
      g_strlcpy(meta.element, GST_OBJECT_NAME(element), sizeof(meta.element));
      g_strlcpy(meta.property, spec->name, sizeof(meta.property));
      meta.step = .01;
      if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
         GParamSpecDouble *v = G_PARAM_SPEC_DOUBLE(spec);
         meta.low = v->minimum; meta.high = v->maximum;
         meta.initial = v->default_value;
      } else if (G_IS_PARAM_SPEC_FLOAT(spec)) {
         GParamSpecFloat *v = G_PARAM_SPEC_FLOAT(spec);
         meta.low = v->minimum; meta.high = v->maximum;
         meta.initial = v->default_value;
      } else if (G_IS_PARAM_SPEC_INT(spec)) {
         GParamSpecInt *v = G_PARAM_SPEC_INT(spec);
         meta.low = v->minimum; meta.high = v->maximum;
         meta.initial = v->default_value; meta.step = 1.;
      } else if (G_IS_PARAM_SPEC_UINT(spec)) {
         GParamSpecUInt *v = G_PARAM_SPEC_UINT(spec);
         meta.low = v->minimum; meta.high = v->maximum;
         meta.initial = v->default_value; meta.step = 1.;
      } else if (G_IS_PARAM_SPEC_BOOLEAN(spec)) {
         GParamSpecBoolean *v = G_PARAM_SPEC_BOOLEAN(spec);
         meta.low = 0.; meta.high = 1.; meta.initial = v->default_value;
         meta.step = 1.;
      } else continue;
      if (!isfinite(meta.low) || !isfinite(meta.high) ||
          meta.high <= meta.low) continue;
      /* The launch description can override the plugin's default.
       * Read that configured value, not merely GParamSpec's default. */
      GValue actual = G_VALUE_INIT;
      g_value_init(&actual, G_PARAM_SPEC_VALUE_TYPE(spec));
      g_object_get_property(G_OBJECT(element), spec->name, &actual);
      if (G_VALUE_HOLDS_DOUBLE(&actual))
         meta.initial = g_value_get_double(&actual);
      else if (G_VALUE_HOLDS_FLOAT(&actual))
         meta.initial = g_value_get_float(&actual);
      else if (G_VALUE_HOLDS_INT(&actual))
         meta.initial = g_value_get_int(&actual);
      else if (G_VALUE_HOLDS_UINT(&actual))
         meta.initial = g_value_get_uint(&actual);
      else if (G_VALUE_HOLDS_BOOLEAN(&actual))
         meta.initial = g_value_get_boolean(&actual) ? 1. : 0.;
      g_value_unset(&actual);
      gchar *id = g_strdup_printf("E|%s|%s|%s",
         meta.worker, meta.element, meta.property);
      gchar *label = g_strdup_printf("%s / %s / %s",
         meta.worker, meta.element, g_param_spec_get_nick(spec));
      routing_effect_port_t *saved = g_new(routing_effect_port_t, 1);
      *saved = meta;
      g_hash_table_replace(ports, g_strdup(id), saved);
      gtk_combo_box_text_append(choice, id, label);
      g_free(label); g_free(id);
   }
   g_free(specs);
}
static void routing_effect_choices(GtkComboBoxText *choice, GHashTable *ports) {
   const struct { const char *config, *worker; } sources[] = {
      { "pipeline:sink.client.mix", "sink.client.dsp0" },
      { "pipeline:src.client.dsp0", "src.client.dsp0" }
   };
   if (!gst_is_initialized()) gst_init(NULL, NULL);
   for (guint s = 0; s < G_N_ELEMENTS(sources); s++) {
      const char *launch = cfg_get(sources[s].config);
      if (!launch || !*launch) continue;
      GError *error = NULL;
      GstElement *pipe = gst_parse_launch(launch, &error);
      if (error) { g_clear_error(&error); if (pipe) gst_object_unref(pipe); continue; }
      if (!pipe) continue;
      if (!GST_IS_BIN(pipe)) { gst_object_unref(pipe); continue; }
      GstIterator *it = gst_bin_iterate_recurse(GST_BIN(pipe));
      GValue value = G_VALUE_INIT;
      while (gst_iterator_next(it, &value) == GST_ITERATOR_OK) {
         GstElement *element = g_value_get_object(&value);
         GstElementFactory *factory = gst_element_get_factory(element);
         const char *provider = factory ? gst_plugin_feature_get_plugin_name(
             GST_PLUGIN_FEATURE(factory)) : NULL;
         if (g_strcmp0(provider, "lv2")) { g_value_reset(&value); continue; }
         routing_lv2_properties(choice, ports, element, sources[s].worker);
         g_value_reset(&value);
      }
      if (G_VALUE_TYPE(&value)) g_value_unset(&value);
      gst_iterator_free(it);
      gst_object_unref(pipe);
   }
   /* Standalone effect instances are owned by Routing, not by a configured
    * capture/playback pipeline. Inspect their factories using a temporary
    * stopped element; "effect" is also the name inside their fwdsp child. */
   for (guint slot = 1; slot <= RR_AUDIO_EFFECT_MAX; slot++) {
      gchar *key = g_strdup_printf("audio.lv2.%u.factory", slot);
      gchar *name = rrclient_database_setting_get(key);
      g_free(key);
      if (!name || !*name) { g_free(name); continue; }
      GstElement *element = gst_element_factory_make(name, "effect");
      if (element) {
         gchar *worker = g_strdup_printf("proc.lv2.%u", slot);
         routing_lv2_properties(choice, ports, element, worker);
         g_free(worker);
         gst_object_unref(element);
      }
      g_free(name);
   }
}
#else
static void routing_effect_choices(GtkComboBoxText *choice, GHashTable *ports) {
   (void)choice; (void)ports;
}
#endif
/* A control-only inventory of the LV2 instances embedded in local
 * GStreamer pipelines. The live DSP remains in fwdsp; these blocks merely
 * expose its named parameter sockets and never move audio. */
typedef struct {
   char *id;
   char worker[64], element[96];
   gboolean standalone;
   GPtrArray *ports; /* routing_effect_port_t, copied from registry metadata */
} routing_effect_block_t;
static GPtrArray *routing_effect_blocks;
static gint routing_effect_drag_index = -1;
static double routing_effect_drag_x, routing_effect_drag_y;
/* Parameter sockets live on the audio-free horizontal edges. Reserve up
 * to three bottom slots initially; remaining controls use the top edge.
 * Wider effects grow to fit every label without overlapping sockets. */
#define ROUTING_EFFECT_W 235.
#define ROUTING_EFFECT_SLOT 106.
#define ROUTING_EFFECT_MARGIN 20.
#define ROUTING_EFFECT_PIN_R 5.
static guint routing_effect_bottom_count(const routing_effect_block_t *block) {
   return MIN(block->ports->len, 3u);
}
static double routing_effect_width(const routing_effect_block_t *block) {
   if (block->standalone) return RR_GTK_GRAPH_NODE_W;
   guint bottom = routing_effect_bottom_count(block);
   guint top = block->ports->len - bottom;
   return MAX(ROUTING_EFFECT_W, 2. * ROUTING_EFFECT_MARGIN +
              MAX(bottom, top) * ROUTING_EFFECT_SLOT);
}
static double routing_effect_height(const routing_effect_block_t *block) {
   if (block->standalone) return RR_GTK_GRAPH_NODE_H;
   return block->ports->len > 3 ? 112. : 90.;
}
/* Shared by rendering, hit-testing and green-wire endpoints. */
static void routing_effect_socket(const routing_effect_block_t *block,
                                   const routing_position_t *pos, guint index,
                                   double *x, double *y, gboolean *top) {
   guint bottom = routing_effect_bottom_count(block);
   *top = index >= bottom;
   guint count = *top ? block->ports->len - bottom : bottom;
   guint column = *top ? index - bottom : index;
   *x = pos->x + (column + 1.) * routing_effect_width(block) / (count + 1.);
   *y = pos->y + (*top ? 0. : routing_effect_height(block));
}
static void routing_effect_block_free(gpointer ptr) {
   routing_effect_block_t *b = ptr;
   g_free(b->id);
   g_ptr_array_unref(b->ports);
   g_free(b);
}
static gint routing_effect_block_cmp(gconstpointer a, gconstpointer b) {
   const routing_effect_block_t *x = *(routing_effect_block_t *const *)a;
   const routing_effect_block_t *y = *(routing_effect_block_t *const *)b;
   return strcmp(x->id, y->id);
}
static gint routing_effect_port_cmp(gconstpointer a, gconstpointer b) {
   const routing_effect_port_t *x = *(routing_effect_port_t *const *)a;
   const routing_effect_port_t *y = *(routing_effect_port_t *const *)b;
   return strcmp(x->property, y->property);
}
static void routing_effect_refresh(void) {
   if (routing_effect_blocks) {
      g_ptr_array_unref(routing_effect_blocks);
      routing_effect_blocks = NULL;
   }
   routing_effect_drag_index = -1;
   routing_effect_blocks = g_ptr_array_new_with_free_func(routing_effect_block_free);
   GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   GtkWidget *dummy = gtk_combo_box_text_new();
   routing_effect_choices(GTK_COMBO_BOX_TEXT(dummy), ports);
   gtk_widget_destroy(dummy);
   GHashTableIter it;
   gpointer value;
   g_hash_table_iter_init(&it, ports);
   while (g_hash_table_iter_next(&it, NULL, &value)) {
      routing_effect_port_t *port = value;
      routing_effect_block_t *block = NULL;
      for (guint i = 0; i < routing_effect_blocks->len; i++) {
         routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
         if (!strcmp(b->worker, port->worker) &&
             !strcmp(b->element, port->element)) {
            block = b; break;
         }
      }
      if (!block) {
         block = g_new0(routing_effect_block_t, 1);
         g_strlcpy(block->worker, port->worker, sizeof(block->worker));
         g_strlcpy(block->element, port->element, sizeof(block->element));
         block->standalone = g_str_has_prefix(block->worker, "proc.lv2.");
         block->id = block->standalone ? g_strdup(block->worker) :
            g_strdup_printf("lv2.%s.%s", block->worker, block->element);
         block->ports = g_ptr_array_new_with_free_func(g_free);
         g_ptr_array_add(routing_effect_blocks, block);
      }
      routing_effect_port_t *copy = g_new(routing_effect_port_t, 1);
      *copy = *port;
      g_ptr_array_add(block->ports, copy);
   }
   g_hash_table_unref(ports);
   g_ptr_array_sort(routing_effect_blocks, routing_effect_block_cmp);
   double y = GRAPH_LAYOUT_MARGIN;
   for (guint i = 0; i < routing_effect_blocks->len; i++) {
      routing_effect_block_t *block = g_ptr_array_index(routing_effect_blocks, i);
      g_ptr_array_sort(block->ports, routing_effect_port_cmp);
      routing_position_t *pos = routing_position(block->id);
      if (!block->standalone && !pos->pinned && !pos->x && !pos->y) {
         pos->x = GRAPH_LAYOUT_MARGIN + GRAPH_LAYOUT_GAP_X * 5.;
         pos->y = y;
      }
      y += routing_effect_height(block) + 24.;
   }
   routing_compact_sync();
   graph_canvas_update_size();
}
static void routing_effect_bounds(double *width, double *height) {
   if (!routing_effect_blocks) return;
   for (guint i = 0; i < routing_effect_blocks->len; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);
      if (b->standalone) continue; /* Already measured as a real PCM node. */
      *width = MAX(*width, pos->x + routing_effect_width(b) + GRAPH_LAYOUT_MARGIN);
      *height = MAX(*height, pos->y + routing_effect_height(b) + GRAPH_LAYOUT_MARGIN);
   }
}
static gboolean routing_effect_parameter_location(const char *worker,
   const char *element, const char *property, double *x, double *y) {
   if (!routing_effect_blocks) return FALSE;
   for (guint i = 0; i < routing_effect_blocks->len; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      if (strcmp(worker, b->worker) || strcmp(element, b->element)) continue;
      routing_position_t *pos = routing_position(b->id);
      for (guint j = 0; j < b->ports->len; j++) {
         const routing_effect_port_t *port = g_ptr_array_index(b->ports, j);
         if (strcmp(port->property, property)) continue;
         gboolean top;
         routing_effect_socket(b, pos, j, x, y, &top);
         return TRUE;
      }
   }
   return FALSE;
}
static gboolean routing_effect_pin_at(double x, double y, routing_effect_port_t *out) {
   if (!routing_controls_visible || !routing_effect_blocks) return FALSE;
   for (guint i = 0; i < routing_effect_blocks->len; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);
      for (guint j = 0; j < b->ports->len; j++) {
         double px, py;
         gboolean top;
         routing_effect_socket(b, pos, j, &px, &py, &top);
         if (hypot(x - px, y - py) <= 12.) {
            *out = *(routing_effect_port_t *)g_ptr_array_index(b->ports, j);
            return TRUE;
         }
      }
   }
   return FALSE;
}
static void routing_effect_draw(cairo_t *cr) {
   if (!routing_controls_visible || !routing_effect_blocks) return;
   for (guint i = 0; i < routing_effect_blocks->len; i++) {
      const routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      const routing_position_t *pos = routing_position(b->id);
      double h = routing_effect_height(b);
      /* The PCM graph draws standalone LV2 blocks when their worker is
       * present in its snapshot. If a worker has not appeared yet, render
       * a complete placeholder body instead of orphan control sockets. */
      gboolean graph_has_block = FALSE;
      if (b->standalone && graph_snapshot)
         for (guint n = 0; n < graph_snapshot->node_count; n++)
            if (!strcmp(graph_snapshot->nodes[n].id, b->id)) {
               graph_has_block = TRUE;
               break;
            }
      if (!graph_has_block) {
         cairo_rectangle(cr, pos->x, pos->y, routing_effect_width(b), h);
         cairo_set_source_rgba(cr, .17, .19, .23, .95);
         cairo_fill_preserve(cr);
         cairo_set_source_rgb(cr, .50, .53, .60);
         cairo_set_line_width(cr, 1.5);
         cairo_stroke(cr);
      }
      cairo_set_source_rgb(cr, .92, .94, .97);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 11.);
      char title[105];
      g_snprintf(title, sizeof(title), "LV2  %.84s", b->element);
      if (!b->standalone || !graph_has_block) {
      /* Top-edge ports claim the first label row. Keep the effect
       * identity centered between upper and lower control labels. */
      double heading_y = pos->y + (b->ports->len > 3 ? 56. : 22.);
      cairo_move_to(cr, pos->x + 13., heading_y);
      cairo_show_text(cr, title);
      cairo_set_font_size(cr, 9.);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_move_to(cr, pos->x + 13., heading_y + 14.);
      cairo_show_text(cr, b->worker);
      }
      for (guint j = 0; j < b->ports->len; j++) {
         const routing_effect_port_t *port = g_ptr_array_index(b->ports, j);
         double px, py;
         gboolean top;
         routing_effect_socket(b, pos, j, &px, &py, &top);
         cairo_set_source_rgb(cr, .24, .84, .38);
         cairo_arc(cr, px, py, ROUTING_EFFECT_PIN_R, 0, 2 * G_PI);
         cairo_fill(cr);
         cairo_set_source_rgb(cr, .92, .94, .97);
         char text[100];
         g_snprintf(text, sizeof(text), b->standalone ? "%.9s" : "%.17s",
                    port->property);
         cairo_text_extents_t ext;
         cairo_text_extents(cr, text, &ext);
         /* Green control labels are vertical on both edges of standalone
          * PCM processors. A local background mask makes the labels readable
          * above the independently drawn control-wire overlay. */
         if (b->standalone) {
            cairo_save(cr);
            cairo_translate(cr, px, py + (top ? -14. : 15.));
            cairo_rotate(cr, top ? -G_PI / 2. : G_PI / 2.);
            cairo_set_font_size(cr, 10.);
            cairo_text_extents(cr, text, &ext);
            double tx = -ext.x_bearing;
            double ty = -ext.y_bearing - ext.height / 2.;
            cairo_set_source_rgba(cr, .12, .13, .16, .96);
            cairo_rectangle(cr, tx + ext.x_bearing - 3.,
               ty + ext.y_bearing - 2., ext.width + 6., ext.height + 4.);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, .92, .94, .97);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, text);
            cairo_restore(cr);
         } else {
            double baseline = py + (top ? 17. : -10.);
            cairo_move_to(cr, px - ext.width / 2. - ext.x_bearing, baseline);
            cairo_show_text(cr, text);
         }
      }
   }
}
static gboolean routing_effect_press(double x, double y, gboolean dbl) {
   if (!routing_controls_visible || !routing_effect_blocks) return FALSE;
   routing_effect_port_t port;
   if (routing_effect_pin_at(x, y, &port)) return TRUE;
   for (gint i = (gint)routing_effect_blocks->len - 1; i >= 0; i--) {
      routing_effect_block_t *b = g_ptr_array_index(routing_effect_blocks, i);
      if (b->standalone) continue; /* Main graph owns audio-block dragging. */
      routing_position_t *pos = routing_position(b->id);
      if (x < pos->x || x > pos->x + routing_effect_width(b) ||
          y < pos->y || y > pos->y + routing_effect_height(b)) continue;
      if (dbl) {
#ifdef RR_HAVE_GSTREAMER
         if (pipeline_editor_widget)
            rr_pipeline_editor_select(pipeline_editor_widget,
              !strcmp(b->worker, "sink.client.dsp0") ?
                "sink.client.mix" : "src.client.dsp0");
#endif
         if (audio_tabs) gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
      } else {
         routing_effect_drag_index = i;
         routing_effect_drag_x = x - pos->x;
         routing_effect_drag_y = y - pos->y;
      }
      return TRUE;
   }
   return FALSE;
}
static gboolean routing_effect_motion(double x, double y) {
   if (!routing_effect_blocks || routing_effect_drag_index < 0 ||
       (guint)routing_effect_drag_index >= routing_effect_blocks->len)
      return FALSE;
   routing_effect_block_t *b =
       g_ptr_array_index(routing_effect_blocks, routing_effect_drag_index);
   routing_position_t *pos = routing_position(b->id);
   pos->x = CLAMP(x - routing_effect_drag_x, 0., 100000.);
   pos->y = CLAMP(y - routing_effect_drag_y, 0., 100000.);
   graph_canvas_update_size();
   return TRUE;
}
static gboolean routing_effect_release(void) {
   if (!routing_effect_blocks || routing_effect_drag_index < 0 ||
       (guint)routing_effect_drag_index >= routing_effect_blocks->len)
      return FALSE;
   routing_effect_block_t *b =
       g_ptr_array_index(routing_effect_blocks, routing_effect_drag_index);
   routing_position_t *pos = routing_position(b->id);
   pos->pinned = TRUE;
   routing_position_save(b->id, pos);
   routing_effect_drag_index = -1;
   graph_canvas_update_size();
   return TRUE;
}
static void routing_compact_set_effect(routing_compact_control_t *control,
                                       const routing_effect_port_t *port) {
   g_strlcpy(control->effect_worker, port->worker, sizeof(control->effect_worker));
   g_strlcpy(control->effect_element, port->element, sizeof(control->effect_element));
   g_strlcpy(control->effect_property, port->property, sizeof(control->effect_property));
   control->id[0] = '\0';
   control->hint_min = port->low;
   control->hint_max = port->high;
   control->hint_step = port->step;
   control->effect_value = port->initial;
   control->effect_default = port->initial;
   routing_compact_apply_hints(control);
   routing_compact_apply_widget(control);
   gtk_widget_set_sensitive(control->value, TRUE);
   routing_compact_save(control);
}
/* Compact controls can target existing mixer gains or locally configured
 * LV2 numeric/boolean properties; server-side effect discovery and mesh
 * parameter transport are separate work. */
static void routing_compact_create(GtkButton *button, gpointer unused) {
   (void)unused;
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Pin Audio Control",
      GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
   GtkWidget *name = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(name), 31);
   gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Auto: use parameter name");
   GtkWidget *target = gtk_combo_box_text_new();
   GHashTable *effect_ports = g_hash_table_new_full(
       g_str_hash, g_str_equal, g_free, g_free);
   GtkWidget *ear = gtk_combo_box_text_new();
   GtkWidget *widget_kind = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ear), "0", "Left");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ear), "1", "Right");
   gtk_combo_box_set_active(GTK_COMBO_BOX(ear), 0);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "horizontal", "Horizontal slider");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "vertical", "Vertical slider");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget_kind), "knob", "Rotary knob");
   gtk_combo_box_set_active(GTK_COMBO_BOX(widget_kind), 0);
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target), "", "Unbound — connect green pin");
   routing_effect_choices(GTK_COMBO_BOX_TEXT(target), effect_ports);
   if (graph_snapshot) {
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
         if (graph_missing_since[i] || !routing_gain_supported(node)) continue;
         char *id = g_strdup_printf("%c%s", routing_gain_input(node) ? 'I' : 'P',
                                    node->id);
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target), id, node->id);
         if (graph_focus_id && !strcmp(graph_focus_id, node->id))
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(target), id);
         g_free(id);
      }
   }
   if (gtk_combo_box_get_active(GTK_COMBO_BOX(target)) < 0)
      gtk_combo_box_set_active(GTK_COMBO_BOX(target), 0);
   GtkWidget *widgets[] = {name, target, ear, widget_kind};
   const char *names[] = {"Label", "Parameter", "Channel", "Widget"};
   for (int i = 0; i < 4; i++) {
      GtkWidget *label = gtk_label_new(names[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), widgets[i], 1, i, 1, 1);
   }
   gtk_widget_show_all(dialog);
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(target));
      const char *channel = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ear));
      const char *kind = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widget_kind));
      if (id && channel && kind) {
         gboolean effect = g_str_has_prefix(id, "E|");
         guint before = routing_compact_controls ? routing_compact_controls->len : 0;
         routing_compact_add(!effect && *id ? id + 1 : "",
             (unsigned)g_ascii_strtoull(channel, NULL, 10),
             !effect && *id == 'P',
             gtk_entry_get_text(GTK_ENTRY(name)), kind, NULL, 0., 0.);
         if (effect && routing_compact_controls &&
             routing_compact_controls->len > before) {
            routing_effect_port_t *meta = g_hash_table_lookup(effect_ports, id);
            if (meta)
               routing_compact_set_effect(g_ptr_array_index(routing_compact_controls,
                    routing_compact_controls->len - 1), meta);
         }
      }
   }
   g_hash_table_unref(effect_ports);
   gtk_widget_destroy(dialog);
}

static gboolean routing_graph_tooltip(GtkWidget *widget, gint px, gint py,
                                      gboolean keyboard, GtkTooltip *tooltip,
                                      gpointer unused) {
   (void)widget;
   (void)unused;
   if (keyboard || !graph_snapshot || graph_scale <= 0) return FALSE;
   const double x = (px - graph_offset_x) / graph_scale;
   const double y = (py - graph_offset_y) / graph_scale;
   routing_effect_port_t control = {0};
   if (routing_effect_pin_at(x, y, &control)) {
      gchar *details = g_strdup_printf(
         "LV2 control: %s / %s\n%s  (%.4g … %.4g)",
         control.worker, control.element, control.property,
         control.low, control.high);
      gtk_tooltip_set_text(tooltip, details);
      g_free(details);
      return TRUE;
   }
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      const double nx = routing_node_x(n);
      const double ny = routing_node_y(n);
      if (x < nx || x > nx + RR_GTK_GRAPH_NODE_W ||
          y < ny || y > ny + RR_GTK_GRAPH_NODE_H)
         continue;
      char from[24], to[24];
      routing_rate_label(n->rate_hz, from, sizeof(from));
      routing_rate_label(n->downstream_rate_hz, to, sizeof(to));
      const char *format = n->sample_format == RR_PCM_F32LE ? "F32LE" :
                           n->sample_format == RR_PCM_S16LE ? "S16LE" : "unknown";
      const char *type = g_str_has_prefix(n->id, "modem.") ?
                         "Modem port (TX/PTT authorization not implemented)" :
                         n->type == RR_AG_SOURCE ? "Source" :
                         n->type == RR_AG_SINK ? "Playback sink" :
                         n->type == RR_AG_MIXER ? "Mix bus" : "Processor";
      gchar *details = g_strdup_printf(
         "%s (%s)\nPCM: %s, %u channel(s), %s%s\n"
         "Output clock: %s%s\nSample-rate conversion: %s\n"
         "Mixer queue: %u ms\nDropped: %" G_GUINT64_FORMAT
         " frames\nStarvation/rebuffers: %" G_GUINT64_FORMAT "%s",
         n->id, type, from, (unsigned)n->channels, format,
         n->rate_requested && n->type == RR_AG_SOURCE ?
            " (requested, not measured)" : "",
         n->downstream_rate_hz ? to : "not specified",
         n->rate_requested && n->type == RR_AG_SINK ?
            " (requested, not measured)" : "",
         n->resampling ? "active" : "none or not known",
         n->queued_ms, n->dropped_frames, n->starvation_events,
         g_str_has_prefix(n->id, "proc.lv2.") ? "\nInput and output require separate patch connections" : n->read_only ? "\nRead-only topology" : "");
      gtk_tooltip_set_text(tooltip, details);
      g_free(details);
      return TRUE;
   }
   return FALSE;
}

static gboolean graph_pointer(double x, double y, double *gx, double *gy) {
   if (!graph_snapshot || graph_scale <= 0) {
      return FALSE;
   }
   *gx = (x - graph_offset_x) / graph_scale;
   *gy = (y - graph_offset_y) / graph_scale;

   return TRUE;
}
static gboolean graph_port_at(double x, double y, gint *source, gint *channel, gboolean *mixer) {
   double radius = CLAMP(10.0 / MAX(.01, graph_scale), 8.0, 20.0);
   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *modem = &graph_snapshot->nodes[i];
      if (!graph_missing_since[i] &&
          g_str_has_prefix(modem->id, "modem.") &&
          g_str_has_suffix(modem->id, ".tx")) {
         double px = routing_node_x(modem) + RR_GTK_GRAPH_NODE_W;
         double py = routing_node_y(modem);
         if (hypot(x - px, y - py - (RR_GTK_GRAPH_NODE_H - 16)) <= radius ||
             hypot(x - px, y - py - 68.) <= radius) {
            *source = (gint)i;
            *channel = fabs(y - py - 68.) <
                       fabs(y - py - (RR_GTK_GRAPH_NODE_H - 16)) ? 0 : 1;
            *mixer = FALSE;
            return TRUE;
         }
      }
      if (graph_snapshot->nodes[i].type == RR_AG_PROCESSOR &&
          g_str_has_prefix(graph_snapshot->nodes[i].id, "proc.lv2.") &&
          !graph_missing_since[i]) {
         const rr_ag_view_node_t *fx = &graph_snapshot->nodes[i];
         for (gint ch = 0; ch < (fx->channels == 2 ? 2 : 1); ch++)
            if (hypot(x - routing_node_x(fx) - RR_GTK_GRAPH_NODE_W,
                      y - routing_node_y(fx) - (fx->channels == 2 ?
                         (ch ? 79. : 60.) : 68.)) <= radius) {
               *source = (gint)i; *channel = ch; *mixer = FALSE;
               return TRUE;
            }
      }
      if (graph_snapshot->nodes[i].type != RR_AG_SOURCE) {
         continue;
      }
      gboolean inactive = graph_missing_since[i] != 0;
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double px = routing_node_x(n) + RR_GTK_GRAPH_NODE_W;
      double py = routing_node_y(n);
      if (graph_snapshot->nodes[i].read_only &&
          strcmp(n->id, "src.client.dsp0")) continue;

      for (gint ch = 0 ; ch < (!strcmp(n->id, "src.client.dsp0") ? 1 : 2) ; ch++) {
         double port_y = !strcmp(n->id, "src.client.dsp0") ? 68. : (ch ? 79. : 60.);
         if (!inactive && hypot(x - px, y - py - port_y) <= radius) {
            *source = (gint) i;
            *channel = ch;
            *mixer = FALSE;

            return TRUE;
         }
      }
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *node = &graph_snapshot->nodes[i];
      gboolean lv2_input = node->type == RR_AG_PROCESSOR &&
          g_str_has_prefix(node->id, "proc.lv2.");
      unsigned modem_slot = 0;
      gboolean modem_input = routing_modem_slot(node, &modem_slot);
      if (graph_missing_since[i] ||
          (node->type != RR_AG_MIXER && !modem_input && !lv2_input))
         continue;
      if (lv2_input) {
         for (gint ch = 0; ch < (node->channels == 2 ? 2 : 1); ch++)
            if (hypot(x - routing_node_x(node),
                      y - routing_node_y(node) - (node->channels == 2 ?
                         (ch ? 79. : 60.) : 68.)) <= radius) {
               *source = -(gint)i - 1; *channel = ch; *mixer = TRUE;
               return TRUE;
            }
         continue;
      }
      if (modem_input) {
         if (hypot(x - routing_node_x(node),
                   y - routing_node_y(node) - 68.) <= radius) {
            *source = -(gint)i - 1;
            *channel = 0;
            *mixer = TRUE;
            return TRUE;
         }
         continue;
      }

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (hypot(x - routing_node_x(node), y - routing_node_y(node) - (ch ? 79 : 60)) <= radius) {
            *source = -(gint)i - 1;
            *channel = ch;
            *mixer = TRUE;

            return TRUE;
         }
      }
   }

   return FALSE;
}
static gboolean graph_gain_at(double x, double y, gint *source,
                              gint *channel, gboolean *port) {
   if (!graph_snapshot) return FALSE;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double left = 0., y0 = routing_node_y(n);
      if (routing_gain_input(n)) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else if (n->type == RR_AG_MIXER) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else if (n->type == RR_AG_SINK) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else continue;
      if (!routing_gain_supported(n) || graph_missing_since[i] ||
          x < left - 8 || x > left + GRAPH_GAIN_W + 8) continue;
      for (gint ch = 0; ch < 2; ch++) {
         if (fabs(y - y0 - (ch ? 79 : 60)) <= 8) {
            *source = (gint)i;
            *channel = ch;
            *port = !routing_gain_input(n);
            return TRUE;
         }
      }
   }
   return FALSE;
}
static void graph_gain_drag(double x) {
   if (!graph_snapshot || gain_drag_source < 0 || gain_drag_channel < 0)
      return;
   rr_ag_view_node_t *node = &graph_snapshot->nodes[gain_drag_source];
   double left = routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
   double ratio = CLAMP((x - left) / GRAPH_GAIN_W, 0., 1.);
   double db = round(GRAPH_GAIN_MIN + ratio * (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN));
   float *current = gain_drag_port ? &node->port_gain_db[gain_drag_channel] :
                                     &node->input_gain_db[gain_drag_channel];
   if (*current == db) return;
   *current = db;
   if (gain_drag_port)
      audio_request_port_gain(node->id, gain_drag_channel, db);
   else
      audio_request_rx_input_gain(node->id, gain_drag_channel, db);
   graph_canvas_update_size();
}
/* Reset on double-click without changing wiring or dragging a port.
 * The ordinary drag path still uses the same per-pin core event dispatch. */
static void graph_gain_reset(gint index, gint channel, gboolean port) {
   if (!graph_snapshot || index < 0 || channel < 0) return;
   rr_ag_view_node_t *node = &graph_snapshot->nodes[index];
   float *current = port ? &node->port_gain_db[channel] :
                           &node->input_gain_db[channel];
   *current = 0.f;
   if (port)
      audio_request_port_gain(node->id, channel, 0.);
   else
      audio_request_rx_input_gain(node->id, channel, 0.);
   graph_canvas_update_size();
}
static unsigned graph_route_mask(char route) {
   return route == 'S' ? 3u : route == 'L' ? 1u : route == 'R' ? 2u : 0u;
}
static void graph_set_channel(guint source, gint channel, gboolean connect) {
   if (!graph_snapshot || source >= graph_snapshot->node_count || graph_missing_since[source] || graph_snapshot->nodes[source].type != RR_AG_SOURCE) {
      return;
   }
   rr_ag_view_node_t *node = &graph_snapshot->nodes[source];
   if (node->read_only) return;
   unsigned bit = channel ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT;
   unsigned mask = graph_route_mask(node->route);
   mask = connect ? mask | bit : mask & ~bit;
   char route = mask == 3 ? 'S' : mask == 1 ? 'L' : mask == 2 ? 'R' : 'M';

   if (route == node->route) {
      return;
   }
   node->route = route;
   graph_profile_modified();
   audio_request_rx_stream_route(node->id, route);
   graph_canvas_update_size();
}

/* Match the actual cubic Bezier curves rather than merely the bounding box. */
static gboolean graph_link_at(double x, double y, guint *source, gint *channel) {
   double limit = CLAMP(7.0 / MAX(.01, graph_scale), 5.0, 17.0);
   double best = limit;
   gboolean found = FALSE;
   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (n->type != RR_AG_SOURCE) {
         continue;
      }
      gboolean inactive = graph_missing_since[i] != 0;
      double x0 = routing_node_x(n) + RR_GTK_GRAPH_NODE_W;
      double y0 = routing_node_y(n);
      unsigned mask = graph_route_mask(n->route);

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (inactive || !(mask & (ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT))) {
            continue;
         }
         const char *dest_id = n->downstream_rate_hz > 16000 ? "hq.mix" : "rx.mix";
         const rr_ag_view_node_t *dest = NULL;
         for (guint j = 0; j < graph_snapshot->node_count; j++)
            if (!strcmp(graph_snapshot->nodes[j].id, dest_id)) {
               dest = &graph_snapshot->nodes[j]; break;
            }
         if (!dest) continue;
         double ya = y0 + (ch ? 79 : 60);
         double yb = routing_node_y(dest) + (ch ? 79 : 60);
         double x1 = routing_node_x(dest);
         double cx = (x0 + x1) / 2;

         for (unsigned k = 0 ; k <= 48 ; k++) {
            double t = (double)k / 48, u = 1 - t;
            double px = u * u * u * x0 + 3 * u * u * t * cx + 3 * u * t * t * cx + t * t * t * x1;
            double py = u * u * u * ya + 3 * u * u * t * ya + 3 * u * t * t * yb + t * t * t * yb;
            double d = hypot(px - x, py - y);

            if (d < best) {
               best = d;
               *source = i;
               *channel = ch;
               found = TRUE;
            }
         }
      }
   }

   return found;
}
static gboolean routing_graph_clicked(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY &&
       graph_snapshot) {
      double gx, gy;
      return graph_pointer(event->x, event->y, &gx, &gy) &&
             (routing_lv2_context_menu(event, gx, gy) ||
              routing_modem_context_menu(event, gx, gy));
   }
   if (event->type == GDK_BUTTON_PRESS &&
       (event->button == GDK_BUTTON_MIDDLE ||
        (event->button == GDK_BUTTON_PRIMARY && (event->state & GDK_MOD1_MASK)))) {
      graph_pan_dragging = TRUE;
      graph_pan_begin_x = event->x; graph_pan_begin_y = event->y;
      graph_pan_base_x = graph_pan_x; graph_pan_base_y = graph_pan_y;
      return TRUE;
   }
   if ((event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) ||
       event->button != GDK_BUTTON_PRIMARY) return FALSE;
   double gx, gy;

   if (!graph_pointer(event->x, event->y, &gx, &gy)) {
      return FALSE;
   }
   if (routing_compact_hit(gx, gy, event->type == GDK_2BUTTON_PRESS))
      return TRUE;
   /* Compact controls have their own persistent graph state; a missing
    * live audio snapshot must not prevent moving or editing them. */
   if (!graph_snapshot) return FALSE;
   if (routing_effect_press(gx, gy, event->type == GDK_2BUTTON_PRESS))
      return TRUE;
   gint source = -1, channel = -1;
   gboolean mixer = FALSE;
   gboolean port_gain = FALSE;
   if (graph_gain_at(gx, gy, &source, &channel, &port_gain)) {
      if (event->type == GDK_2BUTTON_PRESS) {
         gain_drag_source = gain_drag_channel = -1;
         gain_drag_port = FALSE;
         graph_gain_reset(source, channel, port_gain);
         return TRUE;
      }
      gain_drag_port = port_gain;
      gain_drag_source = source;
      gain_drag_channel = channel;
      graph_gain_drag(gx);
      return TRUE;
   }

   /* Route/pipeline cross-navigation is read-only; trim double-click keeps
    * its reset-to-unity behavior above. */
   if (event->type == GDK_2BUTTON_PRESS) {
      graph_node_drag = -1;
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
         if (graph_missing_since[i] || !pipeline_for_node(n->id)) continue;
         double x = routing_node_x(n), y = routing_node_y(n);
         if (gx >= x && gx <= x + RR_GTK_GRAPH_NODE_W &&
             gy >= y && gy <= y + RR_GTK_GRAPH_NODE_H) {
            routing_focus_pipeline(n->id);
            return TRUE;
         }
      }
      return FALSE;
   }

   if (graph_port_at(gx, gy, &source, &channel, &mixer)) {
      graph_drag_source = source;
      graph_drag_channel = channel;
      graph_drag_mixer = mixer;
      graph_drag_start_x = graph_drag_x = gx;
      graph_drag_start_y = graph_drag_y = gy;
      graph_drag_moved = FALSE;

      return TRUE;
   }
   /* Node movement wins over links crossing a block's body. Port and
    * trim gestures still take precedence; link clicks only occur outside
    * blocks to avoid unexpectedly disconnecting audio. */
   for (gint i = (gint)graph_snapshot->node_count - 1; i >= 0; i--) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double nx = routing_node_x(n), ny = routing_node_y(n);
      if (gx >= nx && gx <= nx + RR_GTK_GRAPH_NODE_W &&
          gy >= ny && gy <= ny + RR_GTK_GRAPH_NODE_H) {
         graph_node_drag = i;
         graph_node_drag_x = gx - nx;
         graph_node_drag_y = gy - ny;
         graph_node_press_x = gx;
         graph_node_press_y = gy;
         graph_node_moved = FALSE;
         return TRUE;
      }
   }
   guint linked_source = 0;
   gint linked_channel = -1;
   if (graph_link_at(gx, gy, &linked_source, &linked_channel)) {
      graph_set_channel(linked_source, linked_channel, FALSE);
      return TRUE;
   }

   /* Empty-space left drag pans like qpwgraph; occupied controls retain
    * their own gesture precedence above. */
   graph_pan_dragging = TRUE;
   graph_pan_begin_x = event->x; graph_pan_begin_y = event->y;
   graph_pan_base_x = graph_pan_x; graph_pan_base_y = graph_pan_y;
   return TRUE;
}
static gboolean routing_graph_motion(GtkWidget *widget, GdkEventMotion *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (graph_pan_dragging) {
      graph_pan_x = graph_pan_base_x + event->x - graph_pan_begin_x;
      graph_pan_y = graph_pan_base_y + event->y - graph_pan_begin_y;
      routing_pan_clamp_left();
      graph_canvas_update_size();
      return TRUE;
   }
   if (graph_drag_channel < 0 && gain_drag_source < 0 &&
       graph_node_drag < 0 && !routing_controls_visible) return FALSE;
   double gx, gy;
   if (!graph_pointer(event->x, event->y, &gx, &gy))
      return FALSE;
   if (routing_compact_drag_motion(gx, gy)) return TRUE;
   if (routing_effect_motion(gx, gy)) return TRUE;
   if (gain_drag_source >= 0) {
      graph_gain_drag(gx);
      return TRUE;
   }
   if (graph_drag_channel < 0 && graph_node_drag < 0) return FALSE;
   if (graph_node_drag >= 0 && (guint)graph_node_drag < graph_snapshot->node_count) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[graph_node_drag];
      routing_position_t *pos = routing_position(n->id);
      if (hypot(gx - graph_node_press_x, gy - graph_node_press_y) > 3.)
         graph_node_moved = TRUE;
      if (pos && graph_node_moved) {
         double x = CLAMP(gx - graph_node_drag_x, 0., 100000.);
         double y = CLAMP(gy - graph_node_drag_y, 0., 100000.);
         if (graph_snap_enabled && !(event->state & GDK_SHIFT_MASK)) {
            x = routing_snap_coordinate(x);
            y = routing_snap_coordinate(y);
         }
         pos->x = x;
         pos->y = y;
         graph_canvas_update_size();
      }
      return TRUE;
   }
   graph_drag_moved |= hypot(gx - graph_drag_start_x, gy - graph_drag_start_y) > 4;
   graph_drag_x = gx;
   graph_drag_y = gy;
   graph_canvas_update_size();

   return TRUE;
}
static gboolean routing_graph_released(GtkWidget *widget, GdkEventButton *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (graph_pan_dragging && (event->button == GDK_BUTTON_MIDDLE ||
       event->button == GDK_BUTTON_PRIMARY)) {
      graph_pan_dragging = FALSE;
      return TRUE;
   }
   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   double control_x = 0., control_y = 0.;
   if (graph_pointer(event->x, event->y, &control_x, &control_y) &&
       routing_compact_drag_release(control_x, control_y)) return TRUE;
   if (routing_effect_release()) return TRUE;
   if (graph_node_drag >= 0) {
      if (graph_snapshot && (guint)graph_node_drag < graph_snapshot->node_count) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[graph_node_drag];
         routing_position_t *pos = routing_position(n->id);
         if (pos && graph_node_moved) {
            pos->pinned = TRUE;
            routing_position_save(n->id, pos);
         }
      }
      graph_node_drag = -1;
      graph_node_moved = FALSE;
      routing_layout_auto(FALSE);
      graph_canvas_update_size();
      return TRUE;
   }
   if (gain_drag_source >= 0) {
      double gx, gy;
      if (graph_pointer(event->x, event->y, &gx, &gy))
         graph_gain_drag(gx);
      gain_drag_source = gain_drag_channel = -1;
      return TRUE;
   }
   if (graph_drag_channel < 0) return FALSE;
   double gx, gy, snap_x = 0., snap_y = 0.;
   gint source_index = -1;
   gboolean hit = graph_pointer(event->x, event->y, &gx, &gy) &&
       routing_drag_target(gx, gy, &source_index, &snap_x, &snap_y);
   if (graph_drag_moved && hit && source_index >= 0) {
      if (graph_drag_lv2_target > 0) {
         rr_audio_effect_request_t req = {
            .action = graph_drag_lv2_output ? 'O' : 'R',
            .slot = (unsigned)graph_drag_lv2_target
         };
         req.channel = graph_drag_channel;
         if (graph_drag_lv2_output)
            req.output = graph_drag_lv2_dest_channel ? 'R' : 'L';
         else
            g_strlcpy(req.source, graph_snapshot->nodes[source_index].id,
                      sizeof(req.source));
         audio_request_effect_block(&req);
         routing_queue();
      } else if (graph_drag_rig_target >= 0 && graph_drag_source >= 0) {
         const rr_ag_view_node_t *src = &graph_snapshot->nodes[graph_drag_source];
         const rr_ag_view_node_t *dst = &graph_snapshot->nodes[graph_drag_rig_target];
         if (!strcmp(src->id, "src.client.dsp0") && routing_rx_snapshot) {
            /* Resolve the drawn rig sink back to the stable RX media UUID.
             * The core validates actual TX capability before accepting it. */
            for (guint j = 0; j < routing_rx_snapshot->count; j++) {
               const struct rrclient_rx_stream *rx = &routing_rx_snapshot->streams[j];
               char sink_id[64];
               g_snprintf(sink_id, sizeof(sink_id), "rig.%08x.tx", g_str_hash(rx->id));
               if (strcmp(dst->id, sink_id)) continue;
               audio_request_mic_tx_route(rx->uuid);
               routing_queue();
               break;
            }
         }
         unsigned slot = 0;
         if (sscanf(src->id, "modem.%u.tx", &slot) == 1 &&
             slot >= 1 && slot <= RR_LOCAL_MODEM_MAX) {
            const char *target_id = NULL;
            for (guint i = 0; i < graph_snapshot->node_count; i++) {
               const rr_ag_view_node_t *candidate = &graph_snapshot->nodes[i];
               if (candidate->type != RR_AG_SOURCE ||
                   g_str_has_prefix(candidate->id, "modem.")) continue;
               char hash[64];
               g_snprintf(hash, sizeof(hash), "rig.%08x.", g_str_hash(candidate->id));
               if (g_str_has_prefix(dst->id, hash)) {
                  target_id = candidate->id;
                  break;
               }
            }
            if (target_id) {
               rr_local_modem_request_t req = {
                  .action = graph_drag_channel ? RR_LOCAL_MODEM_PTT_ROUTE :
                                                RR_LOCAL_MODEM_TX_ROUTE,
                  .slot = slot
               };
               g_strlcpy(req.source, target_id, sizeof(req.source));
               rr_local_modem_request(&req);
               routing_queue();
            }
         }
      } else if (graph_drag_modem_target > 0)
         routing_modem_connect((unsigned)graph_drag_modem_target,
            graph_snapshot->nodes[source_index].id);
      else
         graph_set_channel((guint)source_index, graph_drag_channel, TRUE);
   } else if (!graph_drag_moved && !graph_drag_mixer && graph_drag_source >= 0 &&
              !g_str_has_prefix(graph_snapshot->nodes[graph_drag_source].id, "modem.")) {
      unsigned mask = graph_route_mask(graph_snapshot->nodes[graph_drag_source].route);
      unsigned bit = graph_drag_channel ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT;
      graph_set_channel((guint) graph_drag_source, graph_drag_channel, !(mask & bit));
   }
   graph_drag_source = graph_drag_channel = -1;
   graph_drag_modem_target = graph_drag_rig_target = -1;
   graph_drag_lv2_target = -1;
   graph_drag_lv2_output = FALSE;
   graph_drag_lv2_dest_channel = 0;
   graph_drag_mixer = graph_drag_moved = FALSE;
   graph_canvas_update_size();

   return TRUE;
}

/* Route IDs are stable protocol stream IDs; missing streams are skipped on load, so presets remain useful as receivers connect and disconnect. */
#define GRAPH_PROFILE_AUTO "***AUTO***"
#define GRAPH_PROFILE_NEW "New Preset"

static gboolean graph_profile_is_auto(const char *name) {
   return name && !g_ascii_strcasecmp(name, GRAPH_PROFILE_AUTO);
}
static gboolean graph_profile_is_placeholder(const char *name) {
   return !name || !*name || !g_ascii_strcasecmp(name, GRAPH_PROFILE_NEW);
}
static GtkWidget *graph_profile_entry(void) {
   return graph_profiles ? gtk_bin_get_child(GTK_BIN(graph_profiles)) : NULL;
}
static char *graph_profile_selected(void) {
   GtkWidget *entry = graph_profile_entry();

   if (!GTK_IS_ENTRY(entry)) {
      return NULL;
   }
   char *name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
   g_strstrip(name);

   if (!name[0]) {
      g_free(name);

      return NULL;
   }

   return name;
}

typedef struct {
   const char *name;
   gboolean found;
} GraphProfileLookup;
static void graph_profile_lookup_cb(const char *name, void *user) {
   GraphProfileLookup *lookup = user;

   if (!g_ascii_strcasecmp(name, lookup->name)) {
      lookup->found = TRUE;
   }
}
static gboolean graph_profile_exists(const char *name) {
   if (!name || graph_profile_is_auto(name) || graph_profile_is_placeholder(name)) {
      return FALSE;
   }
   GraphProfileLookup lookup = {
      .name = name
   };
   rrclient_database_audio_profile_list(graph_profile_lookup_cb, &lookup);

   return lookup.found;
}
static void graph_profile_update_buttons(void) {
   char *name = graph_profile_selected();
   gboolean editable = !graph_profile_is_auto(name) && !graph_profile_is_placeholder(name);
   gboolean exists = editable && graph_profile_exists(name);

   if (graph_profile_load_button) {
      gtk_widget_set_sensitive(graph_profile_load_button, exists);
   }

   if (graph_profile_save_button) {
      gtk_widget_set_sensitive(graph_profile_save_button, editable && graph_snapshot != NULL);
   }

   if (graph_profile_remove_button) {
      gtk_widget_set_sensitive(graph_profile_remove_button, exists);
   }
   g_free(name);
}
static void graph_profile_entry_changed(GtkEditable *editable, gpointer unused) {
   (void)editable;
   (void)unused;

   if (!graph_profile_updating) {
      graph_profile_update_buttons();
   }
}
static void graph_profile_name(const char *name, void *user) {
   /* Older versions could have persisted reserved display names. Do not expose those rows as writable presets. */
   if (!graph_profile_is_auto(name) && g_ascii_strcasecmp(name, GRAPH_PROFILE_NEW)) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(user), name);
   }
}
static void graph_profile_refresh(const char *selected) {
   if (!graph_profiles) {
      return;
   }
   graph_profile_updating = TRUE;
   gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(graph_profiles));
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(graph_profiles), GRAPH_PROFILE_AUTO);
   rrclient_database_audio_profile_list(graph_profile_name, graph_profiles);
   gtk_entry_set_text(GTK_ENTRY(graph_profile_entry()), selected && *selected ? selected : GRAPH_PROFILE_AUTO);
   graph_profile_updating = FALSE;
   graph_profile_update_buttons();
}
static void graph_profile_modified(void) {
   char *name = graph_profile_selected();

   if (graph_profile_is_auto(name)) {
      graph_profile_updating = TRUE;
      gtk_entry_set_text(GTK_ENTRY(graph_profile_entry()), GRAPH_PROFILE_NEW);
      graph_profile_updating = FALSE;
      graph_profile_update_buttons();
   }
   g_free(name);
}
static gboolean graph_profile_confirm_replace(const char *name) {
   GtkWidget *parent = graph_page ? gtk_widget_get_toplevel(graph_page) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Replace audio preset \"%s\"?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "This will overwrite the saved routes. The preset name remains unique.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Replace", GTK_RESPONSE_ACCEPT, NULL);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   return response == GTK_RESPONSE_ACCEPT;
}
static void graph_profile_save(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!name || graph_profile_is_auto(name) || graph_profile_is_placeholder(name) || !graph_snapshot) {
      g_free(name);

      return;
   }

   if (graph_profile_exists(name) && !graph_profile_confirm_replace(name)) {
      g_free(name);

      return;
   }
   const char *ids[RR_AUDIO_GRAPH_MAX_NODES];
   char routes[RR_AUDIO_GRAPH_MAX_NODES] = {
      0
   };
   size_t count = 0;

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      /* strchr() matches the string terminator for route == '\\0'; do
       * not try to persist synthetic/readonly sources without an actual
       * routing selection. The DB correctly rejects a zero route. */
      if (graph_missing_since[i] || n->type != RR_AG_SOURCE ||
          n->read_only || !n->route || !strchr("SLRM", n->route)) {
         continue;
      }
      ids[count] = n->id;
      routes[count++] = n->route;
   }

   if (rrclient_database_audio_profile_save(name, ids, routes, count)) {
      graph_profile_refresh(name);
   } else {
      g_warning("Unable to save audio route profile '%s'", name);
   }
   g_free(name);
}
static void graph_profile_apply(const char *id, char route, void *unused) {
   (void)unused;
   audio_request_rx_stream_route(id, route);
}
static void graph_profile_load(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!graph_profile_exists(name)) {
      g_free(name);

      return;
   }
   rrclient_database_audio_profile_load(name, graph_profile_apply, NULL);
   routing_queue();
   g_free(name);
}
static void graph_profile_remove(GtkButton *button, gpointer unused) {
   (void)button;
   (void)unused;
   char *name = graph_profile_selected();

   if (!graph_profile_exists(name)) {
      g_free(name);

      return;
   }
   GtkWidget *parent = graph_page ? gtk_widget_get_toplevel(graph_page) : NULL;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove audio profile \"%s\"?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The saved routing profile will be deleted. Active audio routing will not change.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Remove", GTK_RESPONSE_ACCEPT, NULL);
   gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   if (response == GTK_RESPONSE_ACCEPT) {
      if (rrclient_database_audio_profile_delete(name)) {
         graph_profile_refresh(NULL);
      } else {
         g_warning("Unable to remove audio route profile '%s'", name);
      }
   }
   g_free(name);
}
static void routing_auto_layout_clicked(GtkButton *button, gpointer unused) {
   (void)button; (void)unused;
   /* Clear user pins and re-center every active and transient block using
    * the current clock-domain topology. Does not alter audio routing. */
   graph_node_drag = -1;
   graph_pan_x = graph_pan_y = 0.;
   graph_wheel_zoom = 1.;
   routing_layout_auto(TRUE);
   graph_canvas_update_size();
}
/* The toolbar toggles this invisible main-notebook tool page. The page
 * remembers the previous visible tab, without acquiring floating-panel
 * chrome or taking a slot in the application's tab strip. */
void gtk_audio_graph_hide(void) {
   if (!graph_page || !GTK_IS_NOTEBOOK(main_notebook)) return;
   GtkNotebook *book = GTK_NOTEBOOK(main_notebook);
   if (gtk_notebook_get_nth_page(book, gtk_notebook_get_current_page(book)) != graph_page)
      return;
   extern GtkWidget *status_tab;
   gint index = status_tab ? gtk_notebook_page_num(book, status_tab) : -1;
   if (index >= 0) gtk_notebook_set_current_page(book, index);
}
void gtk_audio_graph_toggle(void) {
   if (graph_page && GTK_IS_NOTEBOOK(main_notebook) &&
       gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_notebook),
          gtk_notebook_get_current_page(GTK_NOTEBOOK(main_notebook))) == graph_page) {
      gtk_audio_graph_hide();
      return;
   }
   gtk_audio_graph_present();
}
void gtk_audio_graph_present(void) {
   if (!GTK_IS_NOTEBOOK(main_notebook)) return;
   GtkNotebook *book = GTK_NOTEBOOK(main_notebook);
   if (!graph_page) {
      graph_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      gtk_widget_set_hexpand(graph_page, TRUE);
      gtk_widget_set_vexpand(graph_page, TRUE);
      g_object_set_data(G_OBJECT(graph_page), "rr-tab-hidden", GINT_TO_POINTER(1));
      GtkWidget *label = gtk_label_new("Routing");
      gtk_notebook_append_page(book, graph_page, label);
      gtk_widget_set_no_show_all(label, TRUE);
      gtk_widget_hide(label);
      audio_tabs = gtk_notebook_new();
      gtk_box_pack_start(GTK_BOX(graph_page), audio_tabs, TRUE, TRUE, 0);
      GtkWidget *routing_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs), routing_page, gtk_label_new("Routing"));
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs), pipeline_page_new(), gtk_label_new("Pipelines"));
      GtkWidget *profiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
      graph_profiles = gtk_combo_box_text_new_with_entry();
      g_signal_connect(graph_profile_entry(), "changed", G_CALLBACK(graph_profile_entry_changed), NULL);
      gtk_widget_set_hexpand(graph_profiles, TRUE);
      gtk_widget_set_tooltip_text(graph_profiles, "***AUTO*** is live routing; edit a link to start a New Preset, or choose a saved preset");
      gtk_box_pack_start(GTK_BOX(profiles), graph_profiles, TRUE, TRUE, 0);
      GtkWidget *load = gtk_button_new_with_label("Load");
      GtkWidget *save = gtk_button_new_with_label("Save");
      GtkWidget *remove = gtk_button_new_with_label("Remove");
      graph_profile_load_button = load;
      graph_profile_save_button = save;
      graph_profile_remove_button = remove;
      gtk_widget_set_tooltip_text(load, "Apply the selected profile's stream routes");
      gtk_widget_set_tooltip_text(save, "Save the current routes; replacing an existing preset requires confirmation");
      gtk_widget_set_tooltip_text(remove, "Delete the selected saved profile (confirmation required)");
      g_signal_connect(load, "clicked", G_CALLBACK(graph_profile_load), NULL);
      g_signal_connect(save, "clicked", G_CALLBACK(graph_profile_save), NULL);
      g_signal_connect(remove, "clicked", G_CALLBACK(graph_profile_remove), NULL);
      gtk_box_pack_start(GTK_BOX(profiles), load, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(profiles), save, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(profiles), remove, FALSE, FALSE, 0);
      GtkWidget *auto_layout = gtk_button_new_with_label("Layout");
      gtk_widget_set_tooltip_text(auto_layout,
         "Arrange audio blocks left to right and center connected stages; reset manually saved positions");
      g_signal_connect(auto_layout, "clicked",
                       G_CALLBACK(routing_auto_layout_clicked), NULL);
      gtk_box_pack_start(GTK_BOX(profiles), auto_layout, FALSE, FALSE, 0);
      GtkWidget *snap_grid = gtk_check_button_new_with_label("Snap");
      char *saved_snap = rrclient_database_setting_get(GRAPH_SNAP_KEY);
      graph_snap_enabled = saved_snap &&
         (!g_ascii_strcasecmp(saved_snap, "true") || !strcmp(saved_snap, "1"));
      g_free(saved_snap);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(snap_grid), graph_snap_enabled);
      gtk_widget_set_tooltip_text(snap_grid,
         "Align moved blocks to a 20-pixel grid; hold Shift while dragging to move freely");
      g_signal_connect(snap_grid, "toggled", G_CALLBACK(routing_snap_toggled), NULL);
      gtk_box_pack_start(GTK_BOX(profiles), snap_grid, FALSE, FALSE, 0);
      GtkWidget *show_controls = gtk_check_button_new_with_label("Controls");
      char *show_setting = rrclient_database_setting_get("audio.graph.show-controls");
      routing_controls_visible = !show_setting || strcmp(show_setting, "false");
      g_free(show_setting);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_controls), routing_controls_visible);
      gtk_widget_set_tooltip_text(show_controls, "Show or hide floating controls in Routing");
      g_signal_connect(show_controls, "toggled", G_CALLBACK(routing_show_controls_toggled), NULL);
      gtk_box_pack_end(GTK_BOX(profiles), show_controls, FALSE, FALSE, 0);
      GtkWidget *hidden_controls = gtk_button_new_with_label("Hidden");
      gtk_widget_set_tooltip_text(hidden_controls,
         "Restore hidden floating controls without changing their parameter wiring");
      g_signal_connect(hidden_controls, "clicked",
         G_CALLBACK(routing_compact_hidden_menu), NULL);
      gtk_box_pack_end(GTK_BOX(profiles), hidden_controls, FALSE, FALSE, 0);
      GtkWidget *pin_control = gtk_button_new_with_label("+ Control");
      gtk_widget_set_tooltip_text(pin_control,
         "Create an independent control block; bind its green pin to a parameter");
      g_signal_connect(pin_control, "clicked", G_CALLBACK(routing_compact_create), NULL);
      gtk_box_pack_end(GTK_BOX(profiles), pin_control, FALSE, FALSE, 0);
      GtkWidget *add_effect = gtk_button_new_with_label("+ Effect");
      gtk_widget_set_tooltip_text(add_effect,
         "Add a standalone LV2 processor to Routing, hosted in its own fwdsp subprocess");
      g_signal_connect(add_effect, "clicked", G_CALLBACK(routing_add_effect), NULL);
      gtk_box_pack_end(GTK_BOX(profiles), add_effect, FALSE, FALSE, 0);
      GtkWidget *new_modem = gtk_button_new_with_label("+ Modem");
      gtk_widget_set_tooltip_text(new_modem, "Create an FT8, PSK or Olivia decoder in Routing");
      g_signal_connect(new_modem, "clicked", G_CALLBACK(routing_new_modem), NULL);
      gtk_box_pack_end(GTK_BOX(profiles), new_modem, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(routing_page), profiles, FALSE, FALSE, 0);
      graph_profile_refresh(NULL);
      GtkWidget *info =
         gtk_label_new("Drag blocks to move (Shift bypasses grid snap); drag ports to patch. Middle/Alt-drag pans, wheel zooms. Double-click trim for 0 dB.");
      gtk_label_set_xalign(GTK_LABEL(info), 0);
      gtk_box_pack_start(GTK_BOX(routing_page), info, FALSE, FALSE, 1);
      GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
      /* The graph has its own Cairo zoom. Never scale its viewport size request through the generic widget wrapper as well. */
      g_object_set_data(G_OBJECT(scroller), "rr-scale-exempt", GINT_TO_POINTER(1));
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
      graph_canvas = gtk_drawing_area_new();
      gtk_widget_set_size_request(graph_canvas, 1, 1);
      gtk_widget_set_has_tooltip(graph_canvas, TRUE);
      g_signal_connect(graph_canvas, "query-tooltip", G_CALLBACK(routing_graph_tooltip), NULL);
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
      gtk_box_pack_start(GTK_BOX(routing_page), scroller, TRUE, TRUE, 0);
      gtk_widget_add_events(graph_canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
      g_signal_connect(graph_canvas, "scroll-event", G_CALLBACK(routing_graph_scroll), NULL);
      g_signal_connect(graph_canvas, "button-press-event", G_CALLBACK(routing_graph_clicked), NULL);
      g_signal_connect(graph_canvas, "motion-notify-event", G_CALLBACK(routing_graph_motion), NULL);
      g_signal_connect(graph_canvas, "button-release-event", G_CALLBACK(routing_graph_released), NULL);
      g_signal_connect(graph_canvas, "draw", G_CALLBACK(routing_graph_draw), NULL);
   }
   gtk_widget_show_all(graph_page);
   gtk_notebook_set_current_page(book, gtk_notebook_page_num(book, graph_page));
   routing_queue();             /* Request an up-to-date core graph, not a stale UI cache. */

   if (graph_canvas) {
      gtk_widget_queue_draw(graph_canvas);
   }
}

void gtk_audio_graph_present_pipeline(const char *pipeline_id) {
   gtk_audio_graph_present();
   if (!audio_tabs) return;
#ifdef RR_HAVE_GSTREAMER
   if (pipeline_id && pipeline_editor_widget)
      rr_pipeline_editor_select(pipeline_editor_widget, pipeline_id);
#else
   (void)pipeline_id;
#endif
   gtk_notebook_set_current_page(GTK_NOTEBOOK(audio_tabs), 1);
}

static void routing_snapshot_event(const rr_event_message_t *message, void *unused) {
   (void)unused;

   if (!message || !message->payload || message->length < sizeof(rr_audio_stream_snapshot_t)) {
      return;
   }
   const rr_audio_stream_snapshot_t *snapshot = message->payload;

   if (snapshot->count > 128 || message->length != sizeof(*snapshot) + (size_t)snapshot->count * sizeof(struct rrclient_rx_stream)) {
      return;
   }
   g_free(routing_rx_snapshot);
   routing_rx_snapshot = g_memdup2(snapshot, message->length);
   snapshot_waiting = false;
   snapshot_sent_at = 0;
}

/* A request can be sent before audio_init() installs the core handler. Never let that leave the routing panel permanently waiting for a reply. */
static void routing_queue(void) {
   if (snapshot_waiting && g_get_monotonic_time() - snapshot_sent_at < G_USEC_PER_SEC) {
      return;
   }
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

   /* Refresh topology while visible, even if a stream disappears without producing a route.changed event. The core returns an immutable snapshot;
    * no GTK calls touch the mixer or network thread. */
   if ((graph_page && gtk_widget_get_mapped(graph_page)) || snapshot_waiting) {
      routing_queue();
   }

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

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx))) {
      volume_updating = true;
      gtk_range_set_value(GTK_RANGE(ear_volume[1 - ear]), percent);
      audio_request_rx_channel_volume(1 - ear, percent);
      volume_updating = false;
   }
}

static void routing_changed(GtkWidget *widget, gpointer unused) {
   (void)widget;
   (void)unused;
   gboolean locked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_rx));
   if (locked) {
      volume_updating = true;
      int percent = (int)gtk_range_get_value(GTK_RANGE(ear_volume[0]));
      gtk_range_set_value(GTK_RANGE(ear_volume[1]), percent);
      audio_request_rx_channel_volume(1, percent);
      volume_updating = false;
   }
}

static void routing_live_changed(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)event;
   (void)data;
   (void)connection;
   (void)unused;

   /* Refresh the GUI-owned control-port catalog after a worker is added,
    * removed or rebound. Only view metadata is inspected on GTK. */
   if (graph_page && graph_canvas && gtk_widget_get_mapped(graph_page)) {
      routing_effect_refresh();
      routing_queue();
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
   if (!routing_panel || !routing_window) {
      return;
   }

   if (gtk_widget_get_visible(routing_window)) {
      rr_floating_panel_hide(routing_panel);
   } else {
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
   /* The old vertical faders persisted a much larger floating-window
    * geometry. Preserve placement/preferences but migrate its size once. */
   rr_floating_panel_reset_saved_size(routing_panel);
   routing_window = rr_floating_panel_widget(routing_panel);
   routing_body = rr_floating_panel_body(routing_panel);
   /* Three compact horizontal faders avoid the tall two-column vertical
    * mixer layout. All existing control callbacks and DB keys stay intact. */
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 2);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
   gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, FALSE, FALSE, 0);

   for (unsigned ear = 0; ear < 2; ear++) {
      GtkWidget *label = gtk_label_new(ear ? "R" : "L");
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, ear, 1, 1);
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 112, -1);
      gtk_widget_set_hexpand(ear_volume[ear], FALSE);
      gtk_widget_set_tooltip_text(ear_volume[ear], ear ?
         "Right playback volume" : "Left playback volume");
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]),
         CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], 1, ear, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed",
                       G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   /* RX Gain controls the fwdsp playback sink, not the rig speaker level. */
   GtkWidget *gain_label = gtk_label_new("RX");
   gtk_widget_set_halign(gain_label, GTK_ALIGN_END);
   gtk_widget_set_tooltip_text(gain_label, "RX playback gain (fwdsp)");
   gtk_grid_attach(GTK_GRID(grid), gain_label, 0, 2, 1, 1);
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_vol_slider), FALSE);
   gtk_widget_set_size_request(rx_vol_slider, 112, -1);
   gtk_widget_set_hexpand(rx_vol_slider, FALSE);
   gtk_widget_set_tooltip_text(rx_vol_slider,
      "Client RX playback gain (independent of rig speaker volume)");
   gtk_range_set_value(GTK_RANGE(rx_vol_slider),
      CLAMP(cfg_get_int("audio.volume.rx", 30), 0, 100));
   g_signal_connect(rx_vol_slider, "value-changed",
                    G_CALLBACK(routing_rx_gain_changed), NULL);
   gtk_grid_attach(GTK_GRID(grid), rx_vol_slider, 1, 2, 1, 1);

   split_rx = gtk_check_button_new_with_label("Link L/R");
   gtk_widget_set_halign(split_rx, GTK_ALIGN_START);
   gtk_widget_set_tooltip_text(split_rx,
      "Lock left and right playback volumes together, or adjust independently");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 1, 3, 1, 1);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);

   /* Start linked, preserving the previous non-split default. */
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(split_rx), TRUE);

   gtk_widget_show_all(routing_overlay);
   rr_floating_panel_hide(routing_panel);
   show_token = event_on_domain_token("client.audio.routing.show", routing_show_event, NULL, "gtk");
   snapshot_token = event_on_message_domain_token(RRCLIENT_AUDIO_SNAPSHOT_RESULT, routing_snapshot_event, NULL, "gtk");
   graph_token = event_on_message_domain_token(RRCLIENT_AUDIO_GRAPH_RESULT, routing_graph_snapshot_event, NULL, "gtk");
   routes_token = event_on_domain_token("client.audio.route.changed", routing_live_changed, NULL, "gtk");
   routing_watchdog_source = g_timeout_add(1500, routing_watchdog, NULL);
   routing_queue();
   routing_compact_restore();
   routing_effect_refresh();
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

   if (routes_token) {
      event_off_token(routes_token);
      routes_token = 0;
   }
   g_clear_pointer(&graph_snapshot, g_free);
   routing_control_drag_index = -1;
   if (graph_positions) { g_hash_table_destroy(graph_positions); graph_positions = NULL; }
   graph_node_drag = -1;
   memset(graph_missing_since, 0, sizeof(graph_missing_since));

   if (graph_page) {
      if (GTK_IS_NOTEBOOK(main_notebook)) {
         gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), graph_page);
         if (page >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(main_notebook), page);
      }
      graph_page = NULL;
      graph_canvas = NULL;
      audio_tabs = pipeline_editor_widget = NULL;
      g_clear_pointer(&graph_focus_id, g_free);
      graph_profiles = NULL;
      graph_profile_load_button = graph_profile_save_button = graph_profile_remove_button = NULL;
   }

   if (snapshot_token) {
      event_off_token(snapshot_token);
      snapshot_token = 0;
   }
   g_clear_pointer(&routing_rx_snapshot, g_free);
   snapshot_waiting = false;
   snapshot_sent_at = 0;

   if (show_token) {
      event_off_token(show_token);
      show_token = 0;
   }

   if (routing_compact_controls) {
      /* Flush pending slider values while the full array is still alive;
       * routing_compact_save also serializes the complete UUID index. */
      for (guint i = 0; i < routing_compact_controls->len; i++) {
         routing_compact_control_t *control =
            g_ptr_array_index(routing_compact_controls, i);
         if (control->save_source) {
            g_source_remove(control->save_source);
            control->save_source = 0;
            routing_compact_save(control);
         }
      }
      g_ptr_array_unref(routing_compact_controls);
      routing_compact_controls = NULL;
   }
   if (routing_effect_blocks) {
      g_ptr_array_unref(routing_effect_blocks);
      routing_effect_blocks = NULL;
   }
   routing_effect_drag_index = -1;
   rr_floating_panel_destroy(routing_panel);
   routing_panel = NULL;
   rx_vol_slider = NULL;
   rx_rig_vol_slider = NULL;
   ear_volume[0] = ear_volume[1] = NULL;
   routing_window = routing_overlay = routing_body = split_rx = NULL;
}

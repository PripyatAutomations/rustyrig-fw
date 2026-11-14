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
#include <rrclient/database.h>
#include "gtk.audio-routing.h"
#include "gtk.floating-panel.h"
#include <rrpipelines/editor.h>
/* Uses the Config editor's validated cfg merge and dirty-state handling. */
extern gboolean gtk_pipeline_editor_apply_output(GtkWidget *, const gchar *, gchar **);
extern GtkWidget *main_notebook;

static GtkWidget *routing_window, *split_rx, *ear_volume[2];
static bool volume_updating;
static GtkWidget *routing_overlay, *routing_body;
static RRFloatPanel *routing_panel;
static RRFloatPanel *graph_panel;
static GtkWidget *graph_canvas;
static GtkWidget *audio_tabs, *pipeline_editor_widget;
static char *graph_focus_id;
static GtkWidget *graph_profiles;
static GtkWidget *graph_profile_load_button, *graph_profile_save_button, *graph_profile_remove_button;
static gboolean graph_profile_updating;
static void graph_profile_modified(void);
static void graph_profile_update_buttons(void);
static rr_audio_graph_snapshot_t *graph_snapshot;
/* Preserve display order through transient disconnects. Only GTK owns this history: the core graph and audio routing never contain retired nodes. */
#define GRAPH_GHOST_US (15 * G_USEC_PER_SEC)
static gint64 graph_missing_since[RR_AUDIO_GRAPH_MAX_NODES];
/* Fit the topology to the actual viewport. Panel zoom and wheel zoom multiply independently; neither forces a large GTK size requisition. */
static double graph_zoom = 1.0, graph_wheel_zoom = 1.0;
static double graph_scale = 1.0, graph_offset_x, graph_offset_y;
static void graph_canvas_update_size(void) {
   if (graph_canvas) {
      gtk_widget_queue_draw(graph_canvas);
   }
}
static void routing_graph_zoom(double scale, gpointer unused) {
   (void)unused;
   graph_zoom = scale;
   graph_canvas_update_size();
}
static double routing_node_x(const rr_ag_view_node_t *node) {
   if (node->type == RR_AG_SOURCE) return 25.;
   if (node->type == RR_AG_SINK) return 835.;
   if (!strcmp(node->id, "hq.mix")) return 590.;
   return 355.;
}
static void routing_graph_bounds(double *width, double *height) {
   guint sources = 0;

   if (graph_snapshot) {
      for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
         sources += graph_snapshot->nodes[i].type == RR_AG_SOURCE;
      }
   }
   *width = 1040.0;
   *height = MAX(180.0, 36.0 + 140.0 * sources);
}
static gboolean routing_graph_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer unused) {
   (void)widget;
   (void)unused;
   double step = 0.0;

   if (event->direction == GDK_SCROLL_UP) {
      step = 1.0;
   } else if (event->direction == GDK_SCROLL_DOWN) {
      step = -1.0;
   } else if (event->direction == GDK_SCROLL_SMOOTH) {
      step = -event->delta_y;
   }

   if (!step) {
      return FALSE;
   }
   graph_wheel_zoom = CLAMP(graph_wheel_zoom * pow(1.12, step), 0.10, 6.0);
   graph_canvas_update_size();

   return TRUE;
}

static guint routing_watchdog_source;
static gint64 snapshot_sent_at;
static rr_event_token_t show_token, snapshot_token, graph_token, routes_token;
static bool snapshot_waiting;

/* Initial integrated audio graph view. It renders GTK-owned inventory snapshots only; fwdsp, mixer and supervisor never run in this window. */
static void routing_queue(void);
static gint graph_drag_source = -1, graph_drag_channel = -1;
static gint gain_drag_source = -1, gain_drag_channel = -1;
static gboolean gain_drag_port;
/* Shared source/mixer/sink slider geometry; leave 12px between tracks
 * and pins, so the port remains separately draggable. */
/* Compact L/R output trims reside inside each source block by its pins. */
#define GRAPH_GAIN_X (25.0 + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.0)
#define GRAPH_GAIN_W 46.0
#define GRAPH_GAIN_MIN (-60.0)
#define GRAPH_GAIN_MAX 12.0
static void graph_gain_drag(double x);
static const char *pipeline_for_node(const char *id);
static void routing_focus_pipeline(const char *id);
static gboolean graph_drag_mixer, graph_drag_moved;
static double graph_drag_start_x, graph_drag_start_y, graph_drag_x, graph_drag_y;
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
   guint source_index = 0;
   char format_details[RR_AUDIO_GRAPH_MAX_NODES][96] = {{0}};

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
         char *summary = g_strdup_printf("%s | %s", route, format_details[i]);
         g_strlcpy(format_details[i], summary, sizeof(format_details[i]));
         g_free(summary);
      }
      nodes[i] = (rr_gtk_graph_node_t) {
         .x = routing_node_x(n), .y = source ? 20 + 140 * source_index++ : 80, .title = source
            && n->rig[0] ? n->rig : n->id, .subtitle = source ? n->id : n->type == RR_AG_SINK ? "GStreamer playback" : "Core PCM mixer", .detail =
            graph_missing_since[i] ? "Disconnected" : format_details[i], .failed = n->health == RR_AG_FAILED, .stale =
            graph_missing_since[i] != 0, .selected = !g_strcmp0(graph_focus_id, n->id), .bypass_pins = n->type == RR_AG_PROCESSOR, .stereo_pins = !n->read_only, .io =
            n->type == RR_AG_SOURCE ? RR_GTK_GRAPH_IO_SOURCE : n->type == RR_AG_SINK ? RR_GTK_GRAPH_IO_SINK : RR_GTK_GRAPH_IO_BOTH, .gain_pins = !n->read_only, .fwdsp =
            n->type == RR_AG_SINK && !strcmp(n->id, "sink.client.dsp0")
      };
   }

   guint edge_count = 0;

   for (guint i = 0 ; i < g->link_count ; i++) {
      const rr_ag_view_link_t *link = &g->links[i];

      if (link->src >= g->node_count || link->dst >= g->node_count) {
         continue;
      }
      rr_gtk_graph_edge_kind_t kind = link->kind == RR_AG_BYPASS ? RR_GTK_GRAPH_BYPASS : RR_GTK_GRAPH_NORMAL;

      if (kind == RR_GTK_GRAPH_BYPASS) {
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
   graph_scale = MAX(0.01, fit * graph_zoom * graph_wheel_zoom);
   graph_offset_x = MAX(0.0, (available_w - natural_w * graph_scale) / 2.0);
   graph_offset_y = MAX(0.0, (available_h - natural_h * graph_scale) / 2.0);
   cairo_save(cr);
   cairo_translate(cr, graph_offset_x, graph_offset_y);
   cairo_scale(cr, graph_scale, graph_scale);
   rr_gtk_graph_draw(cr, nodes, g->node_count, edges, edge_count);

   /* Output trims inside each source block; backed by the existing
    * per-stream L/R gains rather than by speaker/master volume. */
   guint trim_row = 0;
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];
      if (node->type != RR_AG_SOURCE) continue;
      double y0 = 20 + 140 * trim_row++;
      if (graph_missing_since[i] || node->read_only) continue;
      for (unsigned channel = 0; channel < 2; channel++) {
         double y = y0 + (channel ? 79 : 60);
         double db = CLAMP(node->input_gain_db[channel],
                           GRAPH_GAIN_MIN, GRAPH_GAIN_MAX);
         double ratio = (db - GRAPH_GAIN_MIN) / (GRAPH_GAIN_MAX - GRAPH_GAIN_MIN);
         cairo_set_line_width(cr, 1.8);
         cairo_set_source_rgba(cr, .55, .67, .76, .85);
         cairo_move_to(cr, GRAPH_GAIN_X, y);
         cairo_line_to(cr, GRAPH_GAIN_X + GRAPH_GAIN_W, y);
         cairo_stroke(cr);
         cairo_set_source_rgb(cr, .31, .82, .97);
         cairo_arc(cr, GRAPH_GAIN_X + GRAPH_GAIN_W * ratio, y, 3.6, 0, 2 * G_PI);
         cairo_fill(cr);
         char value[16];
         g_snprintf(value, sizeof(value), "%+.0f", db);
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8);
         cairo_move_to(cr, GRAPH_GAIN_X - 24, y + 3);
         cairo_show_text(cr, value);
      }
      /* Source diagnostics are read-only and originate from the running
       * mixer status; they are not the negotiated sound-device buffer. */
      char status[80];
      g_snprintf(status, sizeof(status), "Q:%ums D:%" G_GUINT64_FORMAT " U:%" G_GUINT64_FORMAT,
                 node->queued_ms, node->dropped_frames, node->starvation_events);
      cairo_set_source_rgb(cr, .72, .80, .84);
      cairo_set_font_size(cr, 8);
      cairo_move_to(cr, 32, y0 + 101);
      cairo_show_text(cr, status);
   }

   /* Two more independent DSP stages: mixer output trims and sink input
    * trims. Both sit inside their corresponding blocks by the L/R pins. */
   for (guint i = 0; i < g->node_count; i++) {
      const rr_ag_view_node_t *node = &g->nodes[i];
      if (node->type != RR_AG_MIXER && node->type != RR_AG_SINK) continue;
      double node_x = routing_node_x(node);
      double x = node->type == RR_AG_MIXER ?
         node_x + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12. : node_x + 12.;
      if (graph_missing_since[i]) continue;
      for (unsigned channel = 0; channel < 2; channel++) {
         double y = 80 + (channel ? 79 : 60);
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
         char value[16];
         g_snprintf(value, sizeof(value), "%+.0f", db);
         cairo_set_source_rgb(cr, .90, .95, .98);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
         cairo_set_font_size(cr, 8);
         cairo_move_to(cr, x + (node->type == RR_AG_MIXER ? -24. : GRAPH_GAIN_W + 5.), y + 3);
         cairo_show_text(cr, value);
      }
   }

   /* Temporary wire preview while dragging an RX output or mixer input. */
   if (graph_drag_channel >= 0) {
      double x = graph_drag_mixer ? 355.0 : 25.0 + RR_GTK_GRAPH_NODE_W;
      double y = 80.0;

      if (!graph_drag_mixer && graph_drag_source >= 0) {
         guint row = 0;

         for (guint i = 0 ; i < (guint) graph_drag_source && i < g->node_count ; i++) {
            row += g->nodes[i].type == RR_AG_SOURCE;
         }

         y = 20.0 + 140.0 * row;
      }
      y += graph_drag_channel ? 79.0 : 60.0;
      cairo_set_source_rgba(cr, graph_drag_channel ? .73 : .34, graph_drag_channel ? .48 : .78, graph_drag_channel ? 1.0 : .93, .9);
      const double dash[] = {
         5, 4
      };
      cairo_set_dash(cr, dash, 2, 0);
      cairo_set_line_width(cr, 2.5);
      cairo_move_to(cr, x, y);
      cairo_curve_to(cr, (x + graph_drag_x) / 2, y, (x + graph_drag_x) / 2, graph_drag_y, graph_drag_x, graph_drag_y);
      cairo_stroke(cr);
      cairo_set_dash(cr, NULL, 0, 0);
   }
   cairo_restore(cr);

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

static gboolean routing_graph_tooltip(GtkWidget *widget, gint px, gint py,
                                      gboolean keyboard, GtkTooltip *tooltip,
                                      gpointer unused) {
   (void)widget;
   (void)unused;
   if (keyboard || !graph_snapshot || graph_scale <= 0) return FALSE;
   const double x = (px - graph_offset_x) / graph_scale;
   const double y = (py - graph_offset_y) / graph_scale;
   guint source_row = 0;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      const double nx = routing_node_x(n);
      const double ny = n->type == RR_AG_SOURCE ?
                        20 + 140 * source_row++ : 80;
      if (x < nx || x > nx + RR_GTK_GRAPH_NODE_W ||
          y < ny || y > ny + RR_GTK_GRAPH_NODE_H)
         continue;
      char from[24], to[24];
      routing_rate_label(n->rate_hz, from, sizeof(from));
      routing_rate_label(n->downstream_rate_hz, to, sizeof(to));
      const char *format = n->sample_format == RR_PCM_F32LE ? "F32LE" :
                           n->sample_format == RR_PCM_S16LE ? "S16LE" : "unknown";
      const char *type = n->type == RR_AG_SOURCE ? "Source" :
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
         n->read_only ? "\nRead-only topology" : "");
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
   guint row = 0;

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      if (graph_snapshot->nodes[i].type != RR_AG_SOURCE) {
         continue;
      }
      gboolean inactive = graph_missing_since[i] != 0;
      double px = 25 + RR_GTK_GRAPH_NODE_W, py = 20 + 140 * row++;
      if (graph_snapshot->nodes[i].read_only) continue;

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (!inactive && hypot(x - px, y - py - (ch ? 79 : 60)) <= radius) {
            *source = (gint) i;
            *channel = ch;
            *mixer = FALSE;

            return TRUE;
         }
      }
   }

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      if (graph_missing_since[i] || graph_snapshot->nodes[i].type != RR_AG_MIXER || strcmp(graph_snapshot->nodes[i].id, "rx.mix")) {
         continue;
      }

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (hypot(x - 355, y - 80 - (ch ? 79 : 60)) <= radius) {
            *source = -1;
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
   guint row = 0;
   for (guint i = 0; i < graph_snapshot->node_count; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
      double left = 0., y0 = 80.;
      if (n->type == RR_AG_SOURCE) {
         left = GRAPH_GAIN_X;
         y0 = 20 + 140 * row++;
      } else if (n->type == RR_AG_MIXER) {
         left = routing_node_x(n) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12.;
      } else if (n->type == RR_AG_SINK) {
         left = routing_node_x(n) + 12.;
      } else continue;
      if (n->read_only || graph_missing_since[i] ||
          x < left - 8 || x > left + GRAPH_GAIN_W + 8) continue;
      for (gint ch = 0; ch < 2; ch++) {
         if (fabs(y - y0 - (ch ? 79 : 60)) <= 8) {
            *source = (gint)i;
            *channel = ch;
            *port = n->type != RR_AG_SOURCE;
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
   double left = node->type == RR_AG_SOURCE ? GRAPH_GAIN_X :
      node->type == RR_AG_MIXER ?
      routing_node_x(node) + RR_GTK_GRAPH_NODE_W - GRAPH_GAIN_W - 12. :
      routing_node_x(node) + 12.;
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
   guint row = 0;

   for (guint i = 0 ; i < graph_snapshot->node_count ; i++) {
      const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];

      if (n->type != RR_AG_SOURCE) {
         continue;
      }
      gboolean inactive = graph_missing_since[i] != 0;
      double x0 = 25 + RR_GTK_GRAPH_NODE_W, y0 = 20 + 140 * row++;
      unsigned mask = graph_route_mask(n->route);

      for (gint ch = 0 ; ch < 2 ; ch++) {
         if (inactive || !(mask & (ch ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT))) {
            continue;
         }
         double ya = y0 + (ch ? 79 : 60), yb = 80 + (ch ? 79 : 60);
         double x1 = n->downstream_rate_hz > 16000 ? 590. : 355.;
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

   if ((event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) ||
       event->button != GDK_BUTTON_PRIMARY || !graph_snapshot) {
      return FALSE;
   }
   double gx, gy;

   if (!graph_pointer(event->x, event->y, &gx, &gy)) {
      return FALSE;
   }
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
      for (guint i = 0; i < graph_snapshot->node_count; i++) {
         const rr_ag_view_node_t *n = &graph_snapshot->nodes[i];
         if (graph_missing_since[i] || !pipeline_for_node(n->id)) continue;
         gboolean sink = n->type == RR_AG_SINK;
         guint row = 0;
         if (!sink) {
            for (guint j = 0; j < i; j++)
               row += graph_snapshot->nodes[j].type == RR_AG_SOURCE;
         }
         double x = routing_node_x(n), y = sink ? 80 : 20 + 140 * row;
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
   guint linked_source = 0;
   gint linked_channel = -1;

   if (graph_link_at(gx, gy, &linked_source, &linked_channel)) {
      graph_set_channel(linked_source, linked_channel, FALSE);

      return TRUE;
   }

   return FALSE;
}
static gboolean routing_graph_motion(GtkWidget *widget, GdkEventMotion *event, gpointer unused) {
   (void)widget;
   (void)unused;

   if (graph_drag_channel < 0 && gain_drag_source < 0)
      return FALSE;
   double gx, gy;
   if (!graph_pointer(event->x, event->y, &gx, &gy))
      return FALSE;
   if (gain_drag_source >= 0) {
      graph_gain_drag(gx);
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

   if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
   if (gain_drag_source >= 0) {
      double gx, gy;
      if (graph_pointer(event->x, event->y, &gx, &gy))
         graph_gain_drag(gx);
      gain_drag_source = gain_drag_channel = -1;
      return TRUE;
   }
   if (graph_drag_channel < 0) return FALSE;
   double gx, gy;
   gint target = -1, channel = -1;
   gboolean mixer = FALSE;
   gboolean hit = graph_pointer(event->x, event->y, &gx, &gy) && graph_port_at(gx, gy, &target, &channel, &mixer);

   if (graph_drag_moved && hit && channel == graph_drag_channel) {
      /* The executable mixer currently accepts matched L/R RX inputs only. */
      if (graph_drag_mixer && target >= 0) {
         graph_set_channel((guint) target, channel, TRUE);
      } else if (!graph_drag_mixer && mixer && graph_drag_source >= 0) {
         graph_set_channel((guint) graph_drag_source, channel, TRUE);
      }
   } else if (!graph_drag_moved && !graph_drag_mixer && graph_drag_source >= 0) {
      unsigned mask = graph_route_mask(graph_snapshot->nodes[graph_drag_source].route);
      unsigned bit = graph_drag_channel ? RR_AG_CHANNEL_RIGHT : RR_AG_CHANNEL_LEFT;
      graph_set_channel((guint) graph_drag_source, graph_drag_channel, !(mask & bit));
   }
   graph_drag_source = graph_drag_channel = -1;
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
   GtkWidget *parent = graph_panel ? gtk_widget_get_toplevel(rr_floating_panel_widget(graph_panel)) : NULL;
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

      if (graph_missing_since[i] || n->type != RR_AG_SOURCE || !strchr("SLRM", n->route)) {
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
   GtkWidget *parent = graph_panel ? gtk_widget_get_toplevel(rr_floating_panel_widget(graph_panel)) : NULL;
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
static void routing_graph_close(gpointer unused) {
   (void)unused;

   if (graph_panel) {
      rr_floating_panel_hide(graph_panel);
   }
}
void gtk_audio_graph_present(void) {
   if (!routing_overlay || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }

   if (!graph_panel) {
      graph_panel = rr_floating_panel_new(GTK_OVERLAY(routing_overlay), GTK_NOTEBOOK(main_notebook), "Audio Routing", "Audio Routing");

      if (!graph_panel) {
         return;
      }
      rr_floating_panel_set_close_callback(graph_panel, routing_graph_close, NULL);
      rr_floating_panel_set_minimizable(graph_panel, FALSE);
      rr_floating_panel_set_resizable(graph_panel, TRUE);
      rr_floating_panel_set_zoom_callback(graph_panel, routing_graph_zoom, NULL);
      GtkWidget *box = rr_floating_panel_body(graph_panel);
      audio_tabs = gtk_notebook_new();
      gtk_box_pack_start(GTK_BOX(box), audio_tabs, TRUE, TRUE, 0);
      GtkWidget *routing_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs), routing_page, gtk_label_new("Routing"));
      gtk_notebook_append_page(GTK_NOTEBOOK(audio_tabs), pipeline_page_new(), gtk_label_new("Pipelines"));
      GtkWidget *profiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
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
      gtk_box_pack_start(GTK_BOX(routing_page), profiles, FALSE, FALSE, 0);
      graph_profile_refresh(NULL);
      GtkWidget *info =
         gtk_label_new("Drag ports to route audio. Source L/R trims are beside each pin; double-click a slider for 0 dB (unity).");
      gtk_label_set_xalign(GTK_LABEL(info), 0);
      gtk_box_pack_start(GTK_BOX(routing_page), info, FALSE, FALSE, 5);
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
   rr_floating_panel_restore(graph_panel);
   rr_floating_panel_show(graph_panel);
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
   if ((graph_panel && gtk_widget_get_visible(rr_floating_panel_widget(graph_panel))) || snapshot_waiting) {
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

   /* Refresh the graph on external routing edits without changing them. */
   if (graph_panel && graph_canvas && gtk_widget_get_visible(rr_floating_panel_widget(graph_panel))) {
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
   routing_window = rr_floating_panel_widget(routing_panel);
   routing_body = rr_floating_panel_body(routing_panel);
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 4);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
   gtk_box_pack_start(GTK_BOX(routing_body), grid, FALSE, FALSE, 0);

   GtkWidget *volume_label = gtk_label_new("Volume");
   gtk_widget_set_halign(volume_label, GTK_ALIGN_CENTER);
   gtk_grid_attach(GTK_GRID(grid), volume_label, 0, 0, 2, 1);

   /* Both ears are the playback mixer output feeding sink.client.dsp0. */
   for (unsigned ear = 0 ; ear < 2 ; ear++) {
      ear_volume[ear] = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 100, 1);
      gtk_scale_set_draw_value(GTK_SCALE(ear_volume[ear]), FALSE);
      gtk_widget_set_size_request(ear_volume[ear], 40, 132);
      gtk_widget_set_tooltip_text(ear_volume[ear], ear ? "Right playback volume" : "Left playback volume");
      gtk_range_set_value(GTK_RANGE(ear_volume[ear]), CLAMP(cfg_get_int(ear ? "audio.volume.rx.right" : "audio.volume.rx.left", 100), 0, 100));
      gtk_grid_attach(GTK_GRID(grid), ear_volume[ear], ear, 1, 1, 1);
      g_signal_connect(ear_volume[ear], "value-changed", G_CALLBACK(routing_volume_changed), GUINT_TO_POINTER(ear));
   }

   split_rx = gtk_check_button_new_with_label("(Un)Lock");
   gtk_widget_set_halign(split_rx, GTK_ALIGN_CENTER);
   gtk_widget_set_tooltip_text(split_rx, "Lock both playback volumes together, or adjust them independently");
   gtk_grid_attach(GTK_GRID(grid), split_rx, 0, 2, 2, 1);
   g_signal_connect(split_rx, "toggled", G_CALLBACK(routing_changed), NULL);

   /* RX Gain controls the fwdsp playback sink, not the rig speaker level. */
   GtkWidget *gain_label = gtk_label_new("RX Gain");
   gtk_widget_set_halign(gain_label, GTK_ALIGN_CENTER);
   gtk_grid_attach(GTK_GRID(grid), gain_label, 0, 3, 2, 1);
   rx_vol_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   gtk_scale_set_draw_value(GTK_SCALE(rx_vol_slider), FALSE);
   gtk_widget_set_size_request(rx_vol_slider, 112, -1);
   gtk_widget_set_hexpand(rx_vol_slider, TRUE);
   gtk_widget_set_tooltip_text(rx_vol_slider, "Client RX playback gain (independent of rig speaker volume)");
   gtk_range_set_value(GTK_RANGE(rx_vol_slider), CLAMP(cfg_get_int("audio.volume.rx", 30), 0, 100));
   g_signal_connect(rx_vol_slider, "value-changed", G_CALLBACK(routing_rx_gain_changed), NULL);
   gtk_grid_attach(GTK_GRID(grid), rx_vol_slider, 0, 4, 2, 1);

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
   memset(graph_missing_since, 0, sizeof(graph_missing_since));

   if (graph_panel) {
      rr_floating_panel_destroy(graph_panel);
      graph_panel = NULL;
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
   snapshot_waiting = false;
   snapshot_sent_at = 0;

   if (show_token) {
      event_off_token(show_token);
      show_token = 0;
   }

   rr_floating_panel_destroy(routing_panel);
   routing_panel = NULL;
   rx_vol_slider = NULL;
   rx_rig_vol_slider = NULL;
   ear_volume[0] = ear_volume[1] = NULL;
   routing_window = routing_overlay = routing_body = split_rx = NULL;
}

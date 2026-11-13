// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Shared graph canvas primitives for rrclient GTK and rrpipelines. View data only: graph execution, validation and routing belong to core.
 */
#ifndef RRCLIENT_GTK_GRAPH_H
#define RRCLIENT_GTK_GRAPH_H
#include <gtk/gtk.h>
typedef enum {
   RR_GTK_GRAPH_NORMAL,
   RR_GTK_GRAPH_BYPASS
} rr_gtk_graph_edge_kind_t;
typedef enum {
   RR_GTK_GRAPH_IO_BOTH = 0,
   RR_GTK_GRAPH_IO_SOURCE,
   RR_GTK_GRAPH_IO_SINK
} rr_gtk_graph_io_t;
typedef struct {
   double x, y;
   const char *title, *subtitle, *detail;
   gboolean selected, failed, stale, bypass_pins;
   gboolean stereo_pins, fwdsp, gain_pins;
   rr_gtk_graph_io_t io;
} rr_gtk_graph_node_t;
typedef enum {
   RR_GTK_GRAPH_PIN_DEFAULT,
   RR_GTK_GRAPH_PIN_LEFT,
   RR_GTK_GRAPH_PIN_RIGHT
} rr_gtk_graph_pin_t;
typedef struct {
   unsigned from, to;
   rr_gtk_graph_edge_kind_t kind;
   rr_gtk_graph_pin_t src_pin, dst_pin;
} rr_gtk_graph_edge_t;
#define RR_GTK_GRAPH_NODE_W 160
#define RR_GTK_GRAPH_NODE_H 112
/* Pointer-free view objects make it safe to render snapshots produced by other domains. Call on GTK's owning thread only. */
void rr_gtk_graph_draw(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges);
/* Identical audio-graph blocks with top-input / bottom-output signal flow. */
void rr_gtk_graph_draw_vertical(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges);
#endif

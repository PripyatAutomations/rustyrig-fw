// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Shared graph canvas primitives for GTK audio routing and pipeline editing. View data only: graph execution, validation and routing belong to core.
 */
#ifndef RRCLIENT_GTK_GRAPH_H
#define RRCLIENT_GTK_GRAPH_H
#include <gtk/gtk.h>
/* Aesthetic cable droop in logical canvas units, not part of DSP routing. */
void rr_gtk_graph_set_wire_sag(double sag);
double rr_gtk_graph_wire_sag(void);
typedef enum {
   RR_GTK_GRAPH_NORMAL,
   RR_GTK_GRAPH_BYPASS,
   RR_GTK_GRAPH_CONTROL,
   RR_GTK_GRAPH_COS,            /* carrier/synchronization indication, not PTT */
   RR_GTK_GRAPH_NUMBER          /* numeric modulation/control input */
} rr_gtk_graph_edge_kind_t;
typedef enum {
   RR_GTK_GRAPH_IO_BOTH = 0,
   RR_GTK_GRAPH_IO_SOURCE,
   RR_GTK_GRAPH_IO_SINK
} rr_gtk_graph_io_t;
/* Node silhouette, separate from signal type or the application's backend.
 * Defaults to RECT for zero-initialized nodes. Gate polarity is represented
 * with a visual bubble, without changing the red boolean signal type. */
typedef enum {
   RR_GTK_GRAPH_SHAPE_RECT = 0,
   RR_GTK_GRAPH_SHAPE_AND,
   RR_GTK_GRAPH_SHAPE_NAND,
   RR_GTK_GRAPH_SHAPE_OR,
   RR_GTK_GRAPH_SHAPE_NOR,
   RR_GTK_GRAPH_SHAPE_XOR,
   RR_GTK_GRAPH_SHAPE_XNOR,
   RR_GTK_GRAPH_SHAPE_NOT,
   RR_GTK_GRAPH_SHAPE_THRESHOLD,
   RR_GTK_GRAPH_SHAPE_UTILITY
} rr_gtk_graph_shape_t;

/* Named pin placement for forthcoming typed control nodes. The legacy
 * audio pin coordinates are unchanged; keep routing/hit testing in sync. */
typedef enum {
   RR_GTK_GRAPH_SIGNAL_AUDIO,
   RR_GTK_GRAPH_SIGNAL_NUMBER,
   RR_GTK_GRAPH_SIGNAL_BOOLEAN
} rr_gtk_graph_signal_t;
typedef struct {
   const char *name;
   rr_gtk_graph_signal_t signal;
   gboolean output;
   double x, y; /* offsets from node's top-left, within 160x112 cell */
} rr_gtk_graph_port_t;

typedef struct {
   double x, y;
   const char *title, *subtitle, *detail;
   gboolean selected, failed, stale, bypass_pins, hidden; /* View-only filtering. */
   gboolean stereo_pins, fwdsp, gain_pins;
   gboolean ports_only; /* Additional backend endpoint on an existing visual block. */
   gboolean ptt_pin, ptt_input; /* Digital control pins, distinct from PCM. */
   gboolean cos_pin, cos_input; /* Yellow COS outputs and compatible control inputs. */
   rr_gtk_graph_io_t io;
   rr_gtk_graph_shape_t shape; /* Optional: zero preserves old rectangle. */
   const rr_gtk_graph_port_t *ports; /* Optional borrowed port array. */
   guint port_count;
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
   guint8 control_pin; /* RR_GTK_GRAPH_NUMBER: oscillator property input 0..2 */
} rr_gtk_graph_edge_t;
#define RR_GTK_GRAPH_NODE_W 160
#define RR_GTK_GRAPH_NODE_H 112
/* Draw a shape-independent block silhouette and reusable typed pins.
 * Shape helpers do not infer wiring or mutate processing/control state. */
void rr_gtk_graph_shape_path(cairo_t *cr, rr_gtk_graph_shape_t shape,
                             double x, double y, double width, double height);
gboolean rr_gtk_graph_shape_contains(rr_gtk_graph_shape_t shape,
                                      double local_x, double local_y);
/* Return standard geometry for a comparator (five input, two output pins).
 * Caller owns the returned pointer only as a borrowed, static reference. */
const rr_gtk_graph_port_t *rr_gtk_graph_threshold_ports(guint *count);
void rr_gtk_graph_typed_port_draw(cairo_t *cr, double x, double y,
                                  rr_gtk_graph_signal_t signal, gboolean output);

/* Pointer-free view objects make it safe to render snapshots produced by other domains. Call on GTK's owning thread only. */
void rr_gtk_graph_draw(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges);
/* Identical audio-graph blocks with top-input / bottom-output signal flow. */
void rr_gtk_graph_draw_vertical(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges);
#endif

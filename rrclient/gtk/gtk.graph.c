// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "gtk.graph.h"

static void pin(cairo_t *cr, double x, double y, gboolean bypass) {
   if (bypass) {
      cairo_set_source_rgb(cr, .96, .65, .27);
   } else {
      cairo_set_source_rgb(cr, .30, .77, .92);
   }
   cairo_arc(cr, x, y, 5.2, 0, 2 * G_PI);
   cairo_fill(cr);
}
static double pin_y(const rr_gtk_graph_node_t *n, rr_gtk_graph_pin_t channel, gboolean bypass) {
   if (bypass) {
      return n->y + 99;
   }

   if (n->stereo_pins) {
      if (channel == RR_GTK_GRAPH_PIN_RIGHT) {
         return n->y + 79;
      }

      return n->y + 60;
   }

   return n->y + 68;
}
/* Keep the existing audio-routing canvas unchanged. The pipeline editor
 * uses these same nodes and line colors with vertical port placement. */
void rr_gtk_graph_draw_vertical(cairo_t *cr, const rr_gtk_graph_node_t *nodes,
                                guint n_nodes, const rr_gtk_graph_edge_t *edges,
                                guint n_edges) {
   if ((!nodes && n_nodes) || (!edges && n_edges)) return;
   /* Draw the vertical edges first, then the same block primitives through
    * a translated/rotated node drawing pass. */
   cairo_set_line_width(cr, 2.);
   for (guint i = 0; i < n_edges; i++) {
      const rr_gtk_graph_edge_t *e = &edges[i];
      if (e->from >= n_nodes || e->to >= n_nodes) continue;
      const rr_gtk_graph_node_t *a = &nodes[e->from], *b = &nodes[e->to];
      const double sx = a->x + RR_GTK_GRAPH_NODE_W / 2.;
      const double sy = a->y + RR_GTK_GRAPH_NODE_H;
      const double dx = b->x + RR_GTK_GRAPH_NODE_W / 2.;
      const double dy = b->y;
      if (e->kind == RR_GTK_GRAPH_BYPASS) {
         cairo_set_source_rgb(cr, .96, .65, .27);
         const double dash[] = {6, 5};
         cairo_set_dash(cr, dash, 2, 0);
      } else {
         cairo_set_source_rgb(cr, .34, .78, .93);
         cairo_set_dash(cr, NULL, 0, 0);
      }
      cairo_move_to(cr, sx, sy);
      cairo_curve_to(cr, sx, (sy + dy) / 2., dx, (sy + dy) / 2., dx, dy);
      cairo_stroke(cr);
   }
   cairo_set_dash(cr, NULL, 0, 0);
   /* Shared block dimensions, palette and type hierarchy with the audio
    * graph; port dots move to the top and bottom on this canvas. */
   for (guint i = 0; i < n_nodes; i++) {
      const rr_gtk_graph_node_t *n = &nodes[i];
      cairo_set_source_rgb(cr, n->stale ? .23 : n->failed ? .40 :
         n->selected ? .20 : .15,
         n->stale ? .24 : n->failed ? .18 : n->selected ? .38 : .25,
         n->stale ? .26 : n->failed ? .18 : n->selected ? .56 : .34);
      cairo_rectangle(cr, n->x, n->y, RR_GTK_GRAPH_NODE_W, RR_GTK_GRAPH_NODE_H);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, n->selected ? .62 : .53,
         n->selected ? .81 : .65, n->selected ? 1. : .75);
      cairo_set_line_width(cr, n->selected ? 2.8 : 1.3);
      cairo_stroke(cr);
      cairo_set_source_rgb(cr, 1., 1., 1.);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, n->x + 10, n->y + 28);
      cairo_show_text(cr, n->title ? n->title : "Node");
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_move_to(cr, n->x + 10, n->y + 48);
      cairo_show_text(cr, n->subtitle ? n->subtitle : "");
      if (n->detail && *n->detail) {
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 10, n->y + 66);
         cairo_show_text(cr, n->detail);
      }
      cairo_set_source_rgb(cr, .30, .77, .92);
      if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
         cairo_arc(cr, n->x + RR_GTK_GRAPH_NODE_W / 2., n->y, 6., 0., 2 * G_PI);
         cairo_fill(cr);
      }
      if (n->io != RR_GTK_GRAPH_IO_SINK) {
         cairo_arc(cr, n->x + RR_GTK_GRAPH_NODE_W / 2.,
                   n->y + RR_GTK_GRAPH_NODE_H, 6., 0., 2 * G_PI);
         cairo_fill(cr);
      }
   }
}
void rr_gtk_graph_draw(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges) {
   if ((!nodes && n_nodes) || (!edges && n_edges)) {
      return;
   }
   cairo_set_line_width(cr, 2);

   for (guint i = 0 ; i < n_edges ; i++) {
      const rr_gtk_graph_edge_t *e = &edges[i];

      if (e->from >= n_nodes || e->to >= n_nodes) {
         continue;
      }
      const rr_gtk_graph_node_t *a = &nodes[e->from], *b = &nodes[e->to];
      gboolean bypass = e->kind == RR_GTK_GRAPH_BYPASS;
      double from_y = pin_y(a, e->src_pin, bypass);
      double to_y = pin_y(b, e->dst_pin, bypass);

      if (bypass) {
         cairo_set_source_rgb(cr, .96, .65, .27);
         const double dash[] = {
            6, 5
         };
         cairo_set_dash(cr, dash, 2, 0);
      } else {
         /* Use distinct colors even where the two paths run in parallel. */
         if (e->dst_pin == RR_GTK_GRAPH_PIN_RIGHT) {
            cairo_set_source_rgb(cr, .73, .48, 1.0);
         } else {
            cairo_set_source_rgb(cr, .34, .78, .93);
         }
         cairo_set_dash(cr, NULL, 0, 0);
      }
      cairo_move_to(cr, a->x + RR_GTK_GRAPH_NODE_W, from_y);
      cairo_curve_to(cr, (a->x + b->x + RR_GTK_GRAPH_NODE_W) / 2, from_y, (a->x + b->x + RR_GTK_GRAPH_NODE_W) / 2, to_y, b->x, to_y);
      cairo_stroke(cr);
   }

   cairo_set_dash(cr, NULL, 0, 0);

   for (guint i = 0 ; i < n_nodes ; i++) {
      const rr_gtk_graph_node_t *n = &nodes[i];
      cairo_set_source_rgb(cr, n->stale ? .23 : n->failed ? .40 : n->selected ? .20 : .15, n->stale ? .24 : n->failed ? .18 : n->selected ? .38 : .25, n->stale
         ? .26 : n->failed ? .18 : n->selected ? .56 : .34);

      if (n->fwdsp) {
         /* Capsule silhouette distinguishes an external fwdsp/GStreamer process from the in-process mixer and source nodes. */
         double r = 18, x = n->x, y = n->y;
         cairo_new_sub_path(cr);
         cairo_arc(cr, x + RR_GTK_GRAPH_NODE_W - r, y + r, r, -G_PI / 2, 0);
         cairo_arc(cr, x + RR_GTK_GRAPH_NODE_W - r, y + RR_GTK_GRAPH_NODE_H - r, r, 0, G_PI / 2);
         cairo_arc(cr, x + r, y + RR_GTK_GRAPH_NODE_H - r, r, G_PI / 2, G_PI);
         cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
         cairo_close_path(cr);
      } else {
         cairo_rectangle(cr, n->x, n->y, RR_GTK_GRAPH_NODE_W, RR_GTK_GRAPH_NODE_H);
      }
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, n->stale ? .42 : n->failed ? .95 : .53, n->stale ? .43 : n->failed ? .32 : .65, n->stale ? .46 : n->failed ? .28 : .75);
      cairo_set_line_width(cr, n->selected ? 2.8 : 1.3);
      cairo_stroke(cr);
      cairo_set_source_rgb(cr, n->stale ? .66 : 1, n->stale ? .68 : 1, n->stale ? .70 : 1);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, n->x + 10, n->y + 23);
      cairo_show_text(cr, n->title ? n->title : "Node");
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_move_to(cr, n->x + 10, n->y + 39);
      cairo_show_text(cr, n->subtitle ? n->subtitle : "");

      if (n->detail && *n->detail) {
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 10, n->y + 52);
         cairo_show_text(cr, n->detail);
      }

      if (n->fwdsp) {
         cairo_set_source_rgb(cr, .98, .77, .43);
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 116, n->y + 102);
         cairo_show_text(cr, "fwdsp");
      }
      cairo_set_font_size(cr, 10);

      if (n->stereo_pins) {
         /* Both ends have independent L/R ports. Label them at both edges, not just the left, so ear-specific routes are legible. */
         for (unsigned channel = 0 ; channel < 2 ; channel++) {
            rr_gtk_graph_pin_t side = channel ? RR_GTK_GRAPH_PIN_RIGHT : RR_GTK_GRAPH_PIN_LEFT;
            double py = pin_y(n, side, FALSE);

            if (n->stale) {
               cairo_set_source_rgb(cr, .46, .48, .51);
            } else if (channel) {
               cairo_set_source_rgb(cr, .73, .48, 1.0);
            } else {
               cairo_set_source_rgb(cr, .34, .78, .93);
            }

            if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
               cairo_arc(cr, n->x, py, 6.5, 0, 2 * G_PI);
               cairo_fill(cr);
            }

            if (n->io != RR_GTK_GRAPH_IO_SINK) {
               cairo_arc(cr, n->x + RR_GTK_GRAPH_NODE_W, py, 6.5, 0, 2 * G_PI);
               cairo_fill(cr);
            }
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

            if (n->io != RR_GTK_GRAPH_IO_SOURCE && !n->gain_pins) {
               cairo_move_to(cr, n->x + 12, py + 3.5);
               cairo_show_text(cr, channel ? "R IN" : "L IN");
            }

            if (n->io != RR_GTK_GRAPH_IO_SINK && !n->gain_pins) {
               cairo_move_to(cr, n->x + RR_GTK_GRAPH_NODE_W - 43, py + 3.5);
               cairo_show_text(cr, channel ? "R OUT" : "L OUT");
            }
         }
      } else {
         if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
            pin(cr, n->x, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, FALSE), FALSE);
         }

         if (n->io != RR_GTK_GRAPH_IO_SINK) {
            pin(cr, n->x + RR_GTK_GRAPH_NODE_W, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, FALSE), FALSE);
         }
      }

      if (n->bypass_pins) {
         if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
            pin(cr, n->x, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, TRUE), TRUE);
         }

         if (n->io != RR_GTK_GRAPH_IO_SINK) {
            pin(cr, n->x + RR_GTK_GRAPH_NODE_W, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, TRUE), TRUE);
         }
         cairo_set_source_rgb(cr, .96, .65, .27);
         cairo_set_font_size(cr, 8);
         cairo_move_to(cr, n->x + 10, n->y + 102);
         cairo_show_text(cr, "BYPASS");
      }
   }
}

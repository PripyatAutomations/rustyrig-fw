#include "gtk.graph.h"

static void pin(cairo_t *cr, double x, double y, gboolean bypass) {
   if (bypass) cairo_set_source_rgb(cr, .96, .65, .27);
   else cairo_set_source_rgb(cr, .30, .77, .92);
   cairo_arc(cr, x, y, 5.2, 0, 2 * G_PI);
   cairo_fill(cr);
}
void rr_gtk_graph_draw(cairo_t *cr, const rr_gtk_graph_node_t *nodes,
                       guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges) {
   if ((!nodes && n_nodes) || (!edges && n_edges)) return;
   cairo_set_line_width(cr, 2);
   for (guint i = 0; i < n_edges; i++) {
      const rr_gtk_graph_edge_t *e = &edges[i];
      if (e->from >= n_nodes || e->to >= n_nodes) continue;
      const rr_gtk_graph_node_t *a = &nodes[e->from], *b = &nodes[e->to];
      gboolean bypass = e->kind == RR_GTK_GRAPH_BYPASS;
      double yoff = bypass ? 67 : 43;
      if (bypass) {
         cairo_set_source_rgb(cr, .96, .65, .27);
         const double dash[] = {6, 5};
         cairo_set_dash(cr, dash, 2, 0);
      } else {
         cairo_set_source_rgb(cr, .34, .70, .88);
         cairo_set_dash(cr, NULL, 0, 0);
      }
      cairo_move_to(cr, a->x + RR_GTK_GRAPH_NODE_W, a->y + yoff);
      cairo_curve_to(cr, (a->x + b->x + RR_GTK_GRAPH_NODE_W) / 2, a->y + yoff,
                     (a->x + b->x + RR_GTK_GRAPH_NODE_W) / 2, b->y + yoff,
                     b->x, b->y + yoff);
      cairo_stroke(cr);
   }
   cairo_set_dash(cr, NULL, 0, 0);
   for (guint i = 0; i < n_nodes; i++) {
      const rr_gtk_graph_node_t *n = &nodes[i];
      cairo_set_source_rgb(cr, n->failed ? .40 : n->selected ? .20 : .15,
                              n->failed ? .18 : n->selected ? .38 : .25,
                              n->failed ? .18 : n->selected ? .56 : .34);
      cairo_rectangle(cr, n->x, n->y, RR_GTK_GRAPH_NODE_W, RR_GTK_GRAPH_NODE_H);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, n->failed ? .95 : .53, n->failed ? .32 : .65, n->failed ? .28 : .75);
      cairo_set_line_width(cr, n->selected ? 2.8 : 1.3);
      cairo_stroke(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, n->x + 10, n->y + 23);
      cairo_show_text(cr, n->title ? n->title : "Node");
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_move_to(cr, n->x + 10, n->y + 39);
      cairo_show_text(cr, n->subtitle ? n->subtitle : "");
      pin(cr, n->x, n->y + 43, FALSE);
      pin(cr, n->x + RR_GTK_GRAPH_NODE_W, n->y + 43, FALSE);
      if (n->bypass_pins) {
         pin(cr, n->x, n->y + 67, TRUE);
         pin(cr, n->x + RR_GTK_GRAPH_NODE_W, n->y + 67, TRUE);
         cairo_set_source_rgb(cr, .96, .65, .27);
         cairo_set_font_size(cr, 8);
         cairo_move_to(cr, n->x + 9, n->y + 70);
         cairo_show_text(cr, "BYPASS");
      }
   }
}

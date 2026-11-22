// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <rrclient/gtk/gtk.graph.h>

static void check_shape(rr_gtk_graph_shape_t shape) {
   assert(rr_gtk_graph_shape_contains(shape,RR_GTK_GRAPH_NODE_W*.5,
                                          RR_GTK_GRAPH_NODE_H*.5));
   assert(!rr_gtk_graph_shape_contains(shape,-2,20));
   assert(!rr_gtk_graph_shape_contains(shape,RR_GTK_GRAPH_NODE_W+2,20));
   cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,200,150);
   cairo_t *cr=cairo_create(surface);
   rr_gtk_graph_node_t node={.x=10,.y=10,.title="TEST",.shape=shape,
      .io=RR_GTK_GRAPH_IO_SOURCE};
   rr_gtk_graph_draw(cr,&node,1,NULL,0);
   rr_gtk_graph_draw_vertical(cr,&node,1,NULL,0);
   rr_gtk_graph_typed_port_draw(cr,10,20,RR_GTK_GRAPH_SIGNAL_BOOLEAN,FALSE);
   rr_gtk_graph_typed_port_draw(cr,180,20,RR_GTK_GRAPH_SIGNAL_NUMBER,TRUE);
   assert(cairo_status(cr)==CAIRO_STATUS_SUCCESS);
   cairo_destroy(cr);
   cairo_surface_destroy(surface);
}
int main(void) {
   for(unsigned shape=RR_GTK_GRAPH_SHAPE_RECT;
       shape<=RR_GTK_GRAPH_SHAPE_UTILITY;shape++)
      check_shape((rr_gtk_graph_shape_t)shape);
   assert(rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_RECT,2,2));
   assert(!rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_THRESHOLD,2,2));
   assert(!rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_AND,2,2));
   puts("GTK graph logic/threshold/utility silhouettes passed");
   return 0;
}

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
static void check_hidden_nodes(void) {
   /* Hidden source and incident wire must not paint, visible sink must. */
   cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,440,170);
   cairo_t *cr=cairo_create(surface);
   rr_gtk_graph_node_t nodes[2]={
      {.x=10,.y=10,.title="HIDDEN",.hidden=TRUE,.io=RR_GTK_GRAPH_IO_SOURCE},
      {.x=250,.y=10,.title="VISIBLE",.io=RR_GTK_GRAPH_IO_SINK}
   };
   rr_gtk_graph_edge_t edge={.from=0,.to=1,.kind=RR_GTK_GRAPH_NORMAL};
   rr_gtk_graph_draw(cr,nodes,2,&edge,1);
   assert(cairo_status(cr)==CAIRO_STATUS_SUCCESS);
   cairo_surface_flush(surface);
   const unsigned char *pixels=cairo_image_surface_get_data(surface);
   int stride=cairo_image_surface_get_stride(surface);
   /* Only inspect alpha. Pixel (170,70) lies on the hidden wire path. */
   assert(pixels[30*stride+30*4+3]==0);
   assert(pixels[70*stride+175*4+3]==0);
   assert(pixels[30*stride+270*4+3]!=0);
   cairo_destroy(cr);
   cairo_surface_destroy(surface);
}
int main(void) {
   check_hidden_nodes();
   for(unsigned shape=RR_GTK_GRAPH_SHAPE_RECT;
       shape<=RR_GTK_GRAPH_SHAPE_UTILITY;shape++)
      check_shape((rr_gtk_graph_shape_t)shape);
   assert(rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_RECT,2,2));
   assert(!rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_THRESHOLD,2,2));
   assert(!rr_gtk_graph_shape_contains(RR_GTK_GRAPH_SHAPE_AND,2,2));
   guint ports_count=0;
   const rr_gtk_graph_port_t *ports=rr_gtk_graph_threshold_ports(&ports_count);
   assert(ports && ports_count==7);
   assert(!g_strcmp0(ports[4].name,"INVERT"));
   assert(ports[4].signal==RR_GTK_GRAPH_SIGNAL_BOOLEAN && !ports[4].output);
   assert(!g_strcmp0(ports[6].name,"TRUE"));
   assert(ports[6].signal==RR_GTK_GRAPH_SIGNAL_BOOLEAN && ports[6].output);
   cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,220,150);
   cairo_t *cr=cairo_create(surface);
   rr_gtk_graph_node_t threshold={.x=20,.y=15,.title="THRESHOLD",
      .shape=RR_GTK_GRAPH_SHAPE_THRESHOLD,.ports=ports,.port_count=ports_count};
   rr_gtk_graph_draw(cr,&threshold,1,NULL,0);
   assert(cairo_status(cr)==CAIRO_STATUS_SUCCESS);
   cairo_destroy(cr);
   cairo_surface_destroy(surface);
   puts("GTK graph logic/threshold/utility silhouettes and typed pins passed");
   return 0;
}

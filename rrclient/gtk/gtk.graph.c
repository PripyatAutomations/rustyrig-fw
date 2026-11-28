// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include "gtk.graph.h"
#include <librustyui/shape.h>
#include "gtk.wire-sag.h"
#include <math.h>
#include <string.h>

static double graph_wire_sag;
void rr_gtk_graph_set_wire_sag(double sag) {
   graph_wire_sag = isfinite(sag) ? CLAMP(sag,0.,240.) : 0.;
}
double rr_gtk_graph_wire_sag(void) { return graph_wire_sag; }

/* Forward declaration: the shared filled/hollow pin primitive is below. */
static void pin(cairo_t *cr, double x, double y, gboolean bypass, gboolean input);

/* Geometry lives in RustyUI; Cairo is only a path consumer. */
static rrui_shape_t shared_shape(rr_gtk_graph_shape_t shape) {
   switch (shape) {
      case RR_GTK_GRAPH_SHAPE_AND: return RRUI_SHAPE_AND;
      case RR_GTK_GRAPH_SHAPE_NAND: return RRUI_SHAPE_NAND;
      case RR_GTK_GRAPH_SHAPE_OR: return RRUI_SHAPE_OR;
      case RR_GTK_GRAPH_SHAPE_NOR: return RRUI_SHAPE_NOR;
      case RR_GTK_GRAPH_SHAPE_XOR: return RRUI_SHAPE_XOR;
      case RR_GTK_GRAPH_SHAPE_XNOR: return RRUI_SHAPE_XNOR;
      case RR_GTK_GRAPH_SHAPE_NOT: return RRUI_SHAPE_NOT;
      case RR_GTK_GRAPH_SHAPE_THRESHOLD: return RRUI_SHAPE_THRESHOLD;
      case RR_GTK_GRAPH_SHAPE_UTILITY: return RRUI_SHAPE_UTILITY;
      default: return RRUI_SHAPE_RECT;
   }
}
static void cairo_path_command(const rrui_path_command_t *p,void *user) {
   cairo_t *cr=user;
   switch (p->kind) {
      case RRUI_PATH_MOVE: cairo_move_to(cr,p->x1,p->y1); break;
      case RRUI_PATH_LINE: cairo_line_to(cr,p->x1,p->y1); break;
      case RRUI_PATH_CUBIC:
         cairo_curve_to(cr,p->x1,p->y1,p->x2,p->y2,p->x3,p->y3); break;
      case RRUI_PATH_ARC:
         cairo_save(cr);
         cairo_translate(cr,p->x1,p->y1);
         cairo_scale(cr,p->x2,p->y2);
         cairo_arc(cr,0,0,1,p->x3,p->y3);
         cairo_restore(cr);
         break;
      case RRUI_PATH_CLOSE: cairo_close_path(cr); break;
   }
}
void rr_gtk_graph_shape_path(cairo_t *cr,rr_gtk_graph_shape_t shape,
                             double x,double y,double width,double height) {
   if (!cr) { return; }
   cairo_new_path(cr);
   rrui_shape_path(shared_shape(shape),(rrui_shape_bounds_t){x,y,width,height},
                   false,cairo_path_command,cr);
}
static void graph_shape_decoration(cairo_t *cr,rr_gtk_graph_shape_t shape,
                                   double x,double y,double w,double h) {
   cairo_new_path(cr);
   if (rrui_shape_path(shared_shape(shape),(rrui_shape_bounds_t){x,y,w,h},
                      true,cairo_path_command,cr)) {
      cairo_stroke(cr);
   }
}

gboolean rr_gtk_graph_shape_contains(rr_gtk_graph_shape_t shape,
                                      double local_x, double local_y) {
   if (local_x < 0 || local_y < 0 ||
       local_x > RR_GTK_GRAPH_NODE_W || local_y > RR_GTK_GRAPH_NODE_H)
      return FALSE;
   cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_A8,1,1);
   cairo_t *cr=cairo_create(surface);
   rr_gtk_graph_shape_path(cr,shape,0,0,RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H);
   gboolean hit=cairo_in_fill(cr,local_x,local_y);
   cairo_destroy(cr);
   cairo_surface_destroy(surface);
   return hit;
}

const rr_gtk_graph_port_t *rr_gtk_graph_threshold_ports(guint *count) {
   static const rr_gtk_graph_port_t ports[] = {
      {"VALUE",  RR_GTK_GRAPH_SIGNAL_NUMBER,  FALSE, 0,  20},
      {"MIN",    RR_GTK_GRAPH_SIGNAL_NUMBER,  FALSE, 0,  38},
      {"MAX",    RR_GTK_GRAPH_SIGNAL_NUMBER,  FALSE, 0,  56},
      {"SIGNAL", RR_GTK_GRAPH_SIGNAL_BOOLEAN, FALSE, 0,  74},
      {"INVERT", RR_GTK_GRAPH_SIGNAL_BOOLEAN, FALSE, 0,  92},
      {"OUT",    RR_GTK_GRAPH_SIGNAL_BOOLEAN, TRUE,  RR_GTK_GRAPH_NODE_W, 42},
      {"TRUE",   RR_GTK_GRAPH_SIGNAL_BOOLEAN, TRUE,  RR_GTK_GRAPH_NODE_W, 80}
   };
   if (count) *count=G_N_ELEMENTS(ports);
   return ports;
}

void rr_gtk_graph_typed_port_draw(cairo_t *cr, double x, double y,
                                  rr_gtk_graph_signal_t signal, gboolean output) {
   if (!cr) return;
   cairo_save(cr);
   switch (signal) {
      case RR_GTK_GRAPH_SIGNAL_BOOLEAN: cairo_set_source_rgb(cr,.96,.20,.20); break;
      case RR_GTK_GRAPH_SIGNAL_NUMBER: cairo_set_source_rgb(cr,.34,.78,.93); break;
      default: cairo_set_source_rgb(cr,.34,.78,.93); break;
   }
   pin(cr,x,y,FALSE,!output);
   cairo_restore(cr);
}

/* A filled circle sends audio; a hollow, heavy ring receives it. Keep this shared between the horizontal Routing and vertical editor. */
static void pin(cairo_t *cr, double x, double y, gboolean bypass, gboolean input) {
   if (bypass) {
      cairo_set_source_rgb(cr, .96, .65, .27);
   }
   /* Normal pins inherit the left/right channel color from their caller. */
   cairo_new_path(cr);
   cairo_arc(cr, x, y, input ? 6.0 : 5.2, 0, 2 * G_PI);

   if (input) {
      cairo_set_line_width(cr, 3.2);
      cairo_stroke(cr);
   } else {
      cairo_fill(cr);
   }
}

static void graph_arrow(cairo_t *cr, double x, double y, double dx, double dy, double size) {
   double length = hypot(dx, dy);

   if (length < 0.001) {
      return;
   }
   dx /= length;
   dy /= length;
   cairo_new_path(cr);
   cairo_move_to(cr, x, y);
   cairo_line_to(cr, x - size * dx - .52 * size * dy, y - size * dy + .52 * size * dx);
   cairo_line_to(cr, x - size * dx + .52 * size * dy, y - size * dy - .52 * size * dx);
   cairo_close_path(cr);
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

/* Place top/bottom port labels outside their blocks, consistently to
 * the LEFT of the pin rather than centered inside the block silhouette. */
static void graph_vertical_pin_label(cairo_t *cr,const char *label,
                                     double x,double y,gboolean bottom) {
   cairo_save(cr);
   cairo_set_source_rgb(cr,.85,.89,.93);
   cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD);
   cairo_set_font_size(cr,9.);
   cairo_text_extents_t ext;
   cairo_text_extents(cr,label,&ext);
   cairo_move_to(cr,x-ext.width-ext.x_bearing-11.,
                 bottom ? y+16. : y-9.);
   cairo_show_text(cr,label);
   cairo_restore(cr);
}

/* Keep the existing audio-routing canvas unchanged. The pipeline editor uses these same nodes and line colors with vertical port placement. */
void rr_gtk_graph_draw_vertical(cairo_t *cr, const rr_gtk_graph_node_t *nodes, guint n_nodes, const rr_gtk_graph_edge_t *edges, guint n_edges) {
   if ((!nodes && n_nodes) || (!edges && n_edges)) {
      return;
   }
   /* Draw the vertical edges first, then the same block primitives through a translated/rotated node drawing pass. */
   cairo_set_line_width(cr, 2.);

   for (guint i = 0 ; i < n_edges ; i++) {
      const rr_gtk_graph_edge_t *e = &edges[i];

      if (e->from >= n_nodes || e->to >= n_nodes) {
         continue;
      }
      const rr_gtk_graph_node_t *a = &nodes[e->from], *b = &nodes[e->to];
      if(a->hidden || b->hidden)continue;
      const double sx = a->x + RR_GTK_GRAPH_NODE_W / 2.;
      const double sy = a->y + RR_GTK_GRAPH_NODE_H;
      const double dx = b->x + RR_GTK_GRAPH_NODE_W / 2.;
      const double dy = b->y;

      if (e->kind == RR_GTK_GRAPH_COS) {
         cairo_set_source_rgb(cr, 1., .87, .13);
         cairo_set_dash(cr, NULL, 0, 0);
      } else if (e->kind == RR_GTK_GRAPH_CONTROL) {
         cairo_set_source_rgb(cr, .96, .20, .20);
         cairo_set_dash(cr, NULL, 0, 0);
      } else if (e->kind == RR_GTK_GRAPH_BYPASS) {
         cairo_set_source_rgb(cr, .96, .65, .27);
         const double dash[] = {
            6, 5
         };
         cairo_set_dash(cr, dash, 2, 0);
      } else {
         cairo_set_source_rgb(cr, .34, .78, .93);
         cairo_set_dash(cr, NULL, 0, 0);
      }
      cairo_move_to(cr, sx, sy);
      cairo_curve_to(cr, sx, (sy + dy) / 2., dx, (sy + dy) / 2., dx, dy);
      cairo_stroke(cr);
      cairo_set_dash(cr, NULL, 0, 0);
      graph_arrow(cr, dx, dy - 8, 0, 1, 8);
   }

   cairo_set_dash(cr, NULL, 0, 0);

   /* Shared block dimensions, palette and type hierarchy with the audio graph; port dots move to the top and bottom on this canvas. */
   for (guint i = 0 ; i < n_nodes ; i++) {
      const rr_gtk_graph_node_t *n = &nodes[i];
      if(n->hidden)continue;
      cairo_set_source_rgb(cr, n->stale ? .23 : n->failed ? .40 :
         n->selected ? .20 : .15, n->stale ? .24 : n->failed ? .18 : n->selected ? .38 : .25, n->stale ? .26 : n->failed ? .18 : n->selected ? .56 : .34);
      rr_gtk_graph_shape_path(cr, n->shape, n->x, n->y,
                              RR_GTK_GRAPH_NODE_W, RR_GTK_GRAPH_NODE_H);
      cairo_fill_preserve(cr);
      cairo_set_source_rgb(cr, n->selected ? .62 : .53, n->selected ? .81 : .65, n->selected ? 1. : .75);
      cairo_set_line_width(cr, n->selected ? 2.8 : 1.3);
      cairo_stroke(cr);
      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         cairo_set_source_rgb(cr, .53, .65, .75);
         graph_shape_decoration(cr,n->shape,n->x,n->y,
                                RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H);
      }
      cairo_set_source_rgb(cr, 1., 1., 1.);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, n->x + 10, n->y + 28);
      cairo_show_text(cr, n->title ? n->title : "Node");
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_move_to(cr, n->x + 10, n->y + 48);
      cairo_show_text(cr, n->subtitle ? n->subtitle : "");

      if (n->detail && *n->detail) {
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 10, n->y + 66);
         cairo_show_text(cr, n->detail);
      }

      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         for (guint p=0; n->ports && p<n->port_count && p<64; ++p) {
            const rr_gtk_graph_port_t *port=&n->ports[p];
            if (port->x < 0 || port->x > RR_GTK_GRAPH_NODE_W ||
                port->y < 0 || port->y > RR_GTK_GRAPH_NODE_H)
               continue;
            rr_gtk_graph_typed_port_draw(cr,n->x+port->x,n->y+port->y,
                                         port->signal,port->output);
         }
         continue;
      }
      if (n->cos_input) {
         cairo_set_source_rgb(cr, 1., .87, .13);
         pin(cr, n->x + RR_GTK_GRAPH_NODE_W - 12., n->y, FALSE, TRUE);
         graph_vertical_pin_label(cr,"COS",n->x+RR_GTK_GRAPH_NODE_W-12.,n->y,FALSE);
      }

      if (n->cos_pin) {
         cairo_set_source_rgb(cr, 1., .87, .13);
         pin(cr, n->x + RR_GTK_GRAPH_NODE_W - 12., n->y + RR_GTK_GRAPH_NODE_H, FALSE, FALSE);
         graph_vertical_pin_label(cr,"COS",n->x+RR_GTK_GRAPH_NODE_W-12.,
                                  n->y+RR_GTK_GRAPH_NODE_H,TRUE);
      }
      cairo_set_source_rgb(cr, .30, .77, .92);

      if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
         pin(cr, n->x + RR_GTK_GRAPH_NODE_W / 2., n->y, FALSE, TRUE);
         graph_vertical_pin_label(cr,"IN",n->x+RR_GTK_GRAPH_NODE_W/2.,n->y,FALSE);
      }

      if (n->io != RR_GTK_GRAPH_IO_SINK) {
         pin(cr, n->x + RR_GTK_GRAPH_NODE_W / 2., n->y + RR_GTK_GRAPH_NODE_H, FALSE, FALSE);
         graph_vertical_pin_label(cr,"OUT",n->x+RR_GTK_GRAPH_NODE_W/2.,
                                  n->y+RR_GTK_GRAPH_NODE_H,TRUE);
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
      gboolean control = e->kind == RR_GTK_GRAPH_CONTROL;
      gboolean cos = e->kind == RR_GTK_GRAPH_COS;
      gboolean number = e->kind == RR_GTK_GRAPH_NUMBER;
      double from_y = number ? a->y + 68. : cos ? a->y + 14 : control ? a->y + RR_GTK_GRAPH_NODE_H - 16 : pin_y(a, e->src_pin, bypass);
      double to_y = number ? b->y + 55. + 19. * MIN(e->control_pin,2) :
         cos ? b->y + 14 : control ? b->y + RR_GTK_GRAPH_NODE_H - 16 : pin_y(b, e->dst_pin, bypass);

      if (number) {
         cairo_set_source_rgb(cr,.29,.84,.46);
         cairo_set_dash(cr,NULL,0,0);
      } else if (cos) {
         cairo_set_source_rgb(cr, 1., .87, .13);
         cairo_set_dash(cr, NULL, 0, 0);
      } else if (control) {
         cairo_set_source_rgb(cr, .96, .20, .20);
         cairo_set_dash(cr, NULL, 0, 0);
      } else if (bypass) {
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
      const double sx = a->x + RR_GTK_GRAPH_NODE_W;
      /* PCM and PTT receiving sockets are on the block edge. */
      const double ex = b->x - 9.;
      double cx,cy0,cy1;
      rr_patchbay_wire_controls(sx,from_y,ex,to_y,graph_wire_sag,
                                &cx,&cy0,&cy1);
      cairo_move_to(cr, sx, from_y);
      cairo_curve_to(cr, cx, cy0, cx, cy1, ex, to_y);
      cairo_stroke(cr);

      /* Point into the receiving ring, stopping short of its hollow core. */
      if (!bypass) {
         cairo_set_dash(cr, NULL, 0, 0);
         graph_arrow(cr, ex, to_y, 1., 0., 8.);
      }
   }

   cairo_set_dash(cr, NULL, 0, 0);

   for (guint i = 0 ; i < n_nodes ; i++) {
      const rr_gtk_graph_node_t *n = &nodes[i];
      if (n->hidden) { continue; }
      if (!n->ports_only) {
      cairo_set_source_rgb(cr, n->stale ? .23 : n->failed ? .40 : n->selected ? .20 : .15, n->stale ? .24 : n->failed ? .18 : n->selected ? .38 : .25, n->stale
         ? .26 : n->failed ? .18 : n->selected ? .56 : .34);

      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         rr_gtk_graph_shape_path(cr,n->shape,n->x,n->y,
                                 RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H);
      } else if (n->fwdsp) {
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
      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         graph_shape_decoration(cr,n->shape,n->x,n->y,
                                RR_GTK_GRAPH_NODE_W,RR_GTK_GRAPH_NODE_H);
      }
      cairo_set_source_rgb(cr, n->stale ? .66 : 1, n->stale ? .68 : 1, n->stale ? .70 : 1);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 12);
      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         const char *title=n->title ? n->title : "Gate";
         cairo_text_extents_t ext;
         cairo_text_extents(cr,title,&ext);
         cairo_move_to(cr,n->x+RR_GTK_GRAPH_NODE_W*.54-ext.width/2.,
                       n->y+RR_GTK_GRAPH_NODE_H*.50);
      } else {
         cairo_move_to(cr, n->x + 10, n->y + 23);
      }
      cairo_show_text(cr, n->title ? n->title : "Node");
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 10);
      cairo_move_to(cr, n->x + 10, n->y + 39);
      if (n->shape == RR_GTK_GRAPH_SHAPE_RECT)
         cairo_show_text(cr, n->subtitle ? n->subtitle : "");

      if (n->shape == RR_GTK_GRAPH_SHAPE_RECT && n->detail && *n->detail) {
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 10, n->y + 52);
         cairo_show_text(cr, n->detail);
      }

      }
      if(n->title && !strncmp(n->title,"osc.",4)) {
         const char *names[]={"FREQ","LEVEL","WAVE"};
         for(unsigned p=0;p<3;p++) {
            const double y=n->y+55.+19.*p;
            cairo_set_source_rgb(cr,.29,.84,.46);
            pin(cr,n->x,y,FALSE,TRUE);
            cairo_set_source_rgb(cr,.85,.94,.87);
            cairo_set_font_size(cr,8.);
            cairo_move_to(cr,n->x+13,y+3);
            cairo_show_text(cr,names[p]);
         }
      } else if(n->title && !strncmp(n->title,"control.",8)) {
         cairo_set_source_rgb(cr,.29,.84,.46);
         pin(cr,n->x+RR_GTK_GRAPH_NODE_W,n->y+68.,FALSE,FALSE);
         cairo_set_source_rgb(cr,.85,.94,.87);
         cairo_set_font_size(cr,8.);
         cairo_move_to(cr,n->x+RR_GTK_GRAPH_NODE_W-51,n->y+71.);
         cairo_show_text(cr,"VALUE");
      }
      if (n->cos_input) {
         const double px = n->x, py = n->y + 14.;
         cairo_set_source_rgb(cr, 1., .87, .13);
         pin(cr, px, py, FALSE, TRUE);
      }

      if (n->cos_pin) {
         /* Carrier/synchronization detector output; never guessed from the selected AM/FM mode or inferred from the radio's TX flag. */
         const double px = n->x + RR_GTK_GRAPH_NODE_W;
         const double py = n->y + 14.;
         cairo_set_source_rgb(cr, 1., .87, .13);
         pin(cr, px, py, FALSE, FALSE);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
         cairo_set_font_size(cr, 9.);
         cairo_text_extents_t ext;
         cairo_text_extents(cr, "COS", &ext);
         cairo_move_to(cr, px - 11. - ext.width, py - ext.y_bearing - ext.height / 2.);
         cairo_show_text(cr, "COS");
      }

      if (n->ptt_pin) {
         /* Red identifies the dedicated PTT control pin, separate from PCM. */
         const double px = n->x + RR_GTK_GRAPH_NODE_W;
         const double py = n->y + RR_GTK_GRAPH_NODE_H - 16;
         cairo_set_source_rgb(cr, .96, .20, .20);
         pin(cr, px, py, FALSE, FALSE);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
         cairo_set_font_size(cr, 9);
         cairo_text_extents_t extents;
         cairo_text_extents(cr, "PTT", &extents);
         cairo_move_to(cr, px - 11 - extents.width, py - extents.y_bearing - extents.height / 2.);
         cairo_show_text(cr, "PTT");
      }

      if (n->fwdsp) {
         cairo_set_source_rgb(cr, .98, .77, .43);
         cairo_set_font_size(cr, 9);
         cairo_move_to(cr, n->x + 116, n->y + 102);
         cairo_show_text(cr, "fwdsp");
      }
      cairo_set_font_size(cr, 10);

      if (n->shape != RR_GTK_GRAPH_SHAPE_RECT) {
         /* Typed control ports are declared by the graph model; they are not
          * fake audio sockets. All positions stay relative to node geometry. */
         for (guint p=0; n->ports && p<n->port_count && p<64; ++p) {
            const rr_gtk_graph_port_t *port=&n->ports[p];
            if (port->x < 0 || port->x > RR_GTK_GRAPH_NODE_W ||
                port->y < 0 || port->y > RR_GTK_GRAPH_NODE_H)
               continue;
            rr_gtk_graph_typed_port_draw(cr,n->x+port->x,n->y+port->y,
                                         port->signal,port->output);
         }
         continue;
      }

      if(n->title && !strncmp(n->title,"control.",8))
         continue; /* Numeric source has no PCM L/R ports. */
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
               pin(cr, n->x, py, FALSE, TRUE);
            }

            if (n->io != RR_GTK_GRAPH_IO_SINK) {
               pin(cr, n->x + RR_GTK_GRAPH_NODE_W, py, FALSE, FALSE);
            }
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

            if (n->io != RR_GTK_GRAPH_IO_SOURCE && !n->gain_pins) {
               cairo_move_to(cr, n->x + 17, py + 3.5);
               cairo_show_text(cr, channel ? "R IN" : "L IN");
            }

            if (n->io != RR_GTK_GRAPH_IO_SINK && !n->gain_pins) {
               cairo_move_to(cr, n->x + RR_GTK_GRAPH_NODE_W - 51, py + 3.5);
               cairo_show_text(cr, n->title && !strncmp(n->title,"osc.",4) ?
                  (channel ? "Q OUT" : "I OUT") : (channel ? "R OUT" : "L OUT"));
            }
         }
      } else {
         /* The regular PCM input remains on the block edge at mono pin_y. PTT is an additional socket, not a replacement for audio. */
         cairo_set_source_rgb(cr, .30, .77, .92);

         if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
            pin(cr, n->x, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, FALSE), FALSE, TRUE);
         }

         if (n->io != RR_GTK_GRAPH_IO_SINK) {
            pin(cr, n->x + RR_GTK_GRAPH_NODE_W, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, FALSE), FALSE, FALSE);
         }

         if (n->title && strncmp(n->title, "proc.lv2.", 9) == 0) {
            cairo_set_source_rgb(cr, .95, .95, .98);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 9);
            cairo_move_to(cr, n->x + 14, n->y + 71);
            cairo_show_text(cr, "MONO IN");
            cairo_move_to(cr, n->x + RR_GTK_GRAPH_NODE_W - 70, n->y + 71);
            cairo_show_text(cr, "MONO OUT");
         }
      }

      if (n->ptt_input) {
         const double px = n->x;
         const double py = n->y + RR_GTK_GRAPH_NODE_H - 16.;
         cairo_set_source_rgb(cr, .96, .20, .20);
         pin(cr, px, py, FALSE, TRUE);
         cairo_set_source_rgb(cr, 1., 1., 1.);
         cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
         cairo_set_font_size(cr, 9.);
         cairo_move_to(cr, n->x + 9., py + 3.);
         cairo_show_text(cr, "PTT");
      }

      if (n->bypass_pins) {
         if (n->io != RR_GTK_GRAPH_IO_SOURCE) {
            pin(cr, n->x, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, TRUE), TRUE, TRUE);
         }

         if (n->io != RR_GTK_GRAPH_IO_SINK) {
            pin(cr, n->x + RR_GTK_GRAPH_NODE_W, pin_y(n, RR_GTK_GRAPH_PIN_DEFAULT, TRUE), TRUE, FALSE);
         }
         cairo_set_source_rgb(cr, .96, .65, .27);
         cairo_set_font_size(cr, 8);
         cairo_move_to(cr, n->x + 10, n->y + 102);
         cairo_show_text(cr, "BYPASS");
      }
   }
}

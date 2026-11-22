// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_GTK_CONTROL_GRAPH_H
#define RRCLIENT_GTK_CONTROL_GRAPH_H
#include <gtk/gtk.h>
#include <stdbool.h>
typedef gboolean (*rr_logic_source_position_cb)(const char *id,double *x,double *y,gpointer user);
typedef void (*rr_logic_sink_attach_cb)(const char *id,gpointer user);
void rr_logic_canvas_init(rr_logic_source_position_cb position,
                          rr_logic_sink_attach_cb attach,
                          gpointer user);
void rr_logic_canvas_add(GtkWidget *parent);
void rr_logic_canvas_draw(cairo_t *cr);
gboolean rr_logic_canvas_press(double x,double y,GdkEventButton *event);
gboolean rr_logic_canvas_motion(double x,double y,guint modifiers);
gboolean rr_logic_canvas_release(double x,double y,gboolean at_onair);
gboolean rr_logic_canvas_connect_source(const char *source,double x,double y);
gboolean rr_logic_canvas_output_position(const char *name,double *x,double *y);
gboolean rr_logic_canvas_output(const char *name,bool *valid,bool *high);
void rr_logic_canvas_signal(const char *name,bool valid,bool high);
void rr_logic_canvas_reset_sources(void);
void rr_logic_canvas_bounds(double *width,double *height);
#endif

//
// rrpipelines/pipeline.h: GStreamer pipeline graph model.
//    This is part of rustyrig-fw.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRPIPELINES_PIPELINE_H
#define RRPIPELINES_PIPELINE_H

#include <gst/gst.h>

typedef struct rr_pipeline_graph rr_pipeline_graph_t;
typedef struct rr_pipeline_node rr_pipeline_node_t;
typedef struct rr_pipeline_link rr_pipeline_link_t;

struct rr_pipeline_node {
   GstElement *element;
   double x, y;
};

typedef enum {
   RR_PIPELINE_LINK_NORMAL = 0,
   RR_PIPELINE_LINK_BYPASS_IN,
   RR_PIPELINE_LINK_BYPASS_OUT
} rr_pipeline_link_kind_t;

struct rr_pipeline_link {
   guint src, dst;
   gchar *src_pad, *dst_pad;
   rr_pipeline_link_kind_t kind;
};

typedef enum {
   RR_PIPELINE_ERROR_PARSE,
   RR_PIPELINE_ERROR_ELEMENT,
   RR_PIPELINE_ERROR_PROPERTY,
   RR_PIPELINE_ERROR_LINK,
   RR_PIPELINE_ERROR_SERIALIZE
} rr_pipeline_error_t;

#define RR_PIPELINE_ERROR rr_pipeline_error_quark()
GQuark rr_pipeline_error_quark(void);

rr_pipeline_graph_t *rr_pipeline_graph_parse(const gchar *launch, GError **error);
rr_pipeline_graph_t *rr_pipeline_graph_new(void);
void rr_pipeline_graph_free(rr_pipeline_graph_t *graph);
guint rr_pipeline_graph_node_count(const rr_pipeline_graph_t *graph);
guint rr_pipeline_graph_link_count(const rr_pipeline_graph_t *graph);
const rr_pipeline_node_t *rr_pipeline_graph_node(const rr_pipeline_graph_t *graph, guint index);
rr_pipeline_node_t *rr_pipeline_graph_node_edit(rr_pipeline_graph_t *graph, guint index);
const rr_pipeline_link_t *rr_pipeline_graph_link(const rr_pipeline_graph_t *graph, guint index);
const gchar *rr_pipeline_node_factory(const rr_pipeline_node_t *node);
const gchar *rr_pipeline_node_name(const rr_pipeline_node_t *node);
void rr_pipeline_node_set_position(rr_pipeline_node_t *node, double x, double y);
gboolean rr_pipeline_graph_add_element(rr_pipeline_graph_t *graph, const gchar *factory, double x, double y, guint *index, GError **error);
gboolean rr_pipeline_graph_add_link(rr_pipeline_graph_t *graph, guint src, guint dst, GError **error);
/* Conditional supervisor routing: not GStreamer pads or launch-string connections. */
gboolean rr_pipeline_graph_add_bypass_link(rr_pipeline_graph_t *graph, guint src, guint dst, rr_pipeline_link_kind_t kind, GError **error);
gboolean rr_pipeline_graph_remove_node(rr_pipeline_graph_t *graph, guint index, GError **error);
gboolean rr_pipeline_graph_set_property(rr_pipeline_graph_t *graph, guint node, const gchar *property, const gchar *value, GError **error);
gboolean rr_pipeline_graph_add_tee(rr_pipeline_graph_t *graph, guint source, guint *tee_index, GError **error);
gboolean rr_pipeline_graph_add_branch(rr_pipeline_graph_t *graph, guint tee, const gchar *sink_factory, GError **error);
gchar *rr_pipeline_graph_serialize(const rr_pipeline_graph_t *graph, GError **error);

#endif

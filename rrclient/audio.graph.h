/* Logical audio routing graph: control-plane model, no GTK or GStreamer.
 * Runtime processing remains in fwdsp; graph mutation belongs to core.
 */
#ifndef RRCLIENT_AUDIO_GRAPH_H
#define RRCLIENT_AUDIO_GRAPH_H
#include <glib.h>
#include <stdbool.h>
typedef struct rr_audio_graph rr_audio_graph_t;
typedef enum {
   RR_AG_SOURCE, RR_AG_PROCESSOR, RR_AG_MIXER, RR_AG_SINK
} rr_ag_node_type_t;
typedef enum {
   RR_AG_NORMAL = 0, RR_AG_BYPASS
} rr_ag_link_type_t;
typedef enum {
   RR_AG_STOPPED, RR_AG_STARTING, RR_AG_HEALTHY, RR_AG_FAILED
} rr_ag_health_t;
rr_audio_graph_t *rr_audio_graph_new(void);
void rr_audio_graph_free(rr_audio_graph_t *graph);
bool rr_audio_graph_add_node(rr_audio_graph_t *graph, const char *id, rr_ag_node_type_t type);
bool rr_audio_graph_add_link(rr_audio_graph_t *graph, const char *src, const char *dst, rr_ag_link_type_t type);
bool rr_audio_graph_set_health(rr_audio_graph_t *graph, const char *id, rr_ag_health_t health);
/* Resolve one selected input; NULL means muted/no usable route. The BYPASS
 * input is only selected when the normal source is FAILED, never mixed with it. */
const char *rr_audio_graph_input(const rr_audio_graph_t *graph, const char *dst);
guint rr_audio_graph_node_count(const rr_audio_graph_t *graph);
guint rr_audio_graph_link_count(const rr_audio_graph_t *graph);
#endif

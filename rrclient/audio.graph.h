//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Logical audio routing graph: control-plane model, no GTK or GStreamer. Runtime processing remains in fwdsp; graph mutation belongs to core.
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
#define RR_AG_CHANNEL_LEFT 1u
#define RR_AG_CHANNEL_RIGHT 2u
#define RR_AG_CHANNEL_STEREO (RR_AG_CHANNEL_LEFT | RR_AG_CHANNEL_RIGHT)
typedef enum {
   RR_AG_STOPPED, RR_AG_STARTING, RR_AG_HEALTHY, RR_AG_FAILED
} rr_ag_health_t;
rr_audio_graph_t *rr_audio_graph_new(void);
void rr_audio_graph_free(rr_audio_graph_t *graph);
bool rr_audio_graph_add_node(rr_audio_graph_t *graph, const char *id, rr_ag_node_type_t type);
bool rr_audio_graph_add_link(rr_audio_graph_t *graph, const char *src, const char *dst, rr_ag_link_type_t type);
bool rr_audio_graph_add_channel_link(rr_audio_graph_t *graph, const char *src, const char *dst, rr_ag_link_type_t type, unsigned channels);
bool rr_audio_graph_set_health(rr_audio_graph_t *graph, const char *id, rr_ag_health_t health);
bool rr_audio_graph_set_route(rr_audio_graph_t *graph, const char *id, char route);
/* Resolve one selected input; NULL means muted/no usable route. The BYPASS input is only selected when the normal source is FAILED, never mixed with it. */
const char *rr_audio_graph_input(const rr_audio_graph_t *graph, const char *dst);
/* Value-copy topology for frontends. Indices are local to each snapshot. */
#define RR_AUDIO_GRAPH_MAX_NODES 34
#define RR_AUDIO_GRAPH_MAX_LINKS 64
#define RRCLIENT_AUDIO_GRAPH_RESULT "client.audio.graph.result"
typedef struct {
   char id[64];
   rr_ag_node_type_t type;
   rr_ag_health_t health;
   char route;
   char rig[96]; /* Display identity for an RX stream (site.rig), not its routing ID. */
} rr_ag_view_node_t;
typedef struct {
   guint32 src, dst;
   rr_ag_link_type_t kind;
   guint8 channels;             /* L=1, R=2; both use separate stereo pins */
} rr_ag_view_link_t;
typedef struct {
   guint32 node_count, link_count;
   rr_ag_view_node_t nodes[RR_AUDIO_GRAPH_MAX_NODES];
   rr_ag_view_link_t links[RR_AUDIO_GRAPH_MAX_LINKS];
} rr_audio_graph_snapshot_t;
bool rr_audio_graph_snapshot(const rr_audio_graph_t *graph, rr_audio_graph_snapshot_t *out);
guint rr_audio_graph_node_count(const rr_audio_graph_t *graph);
guint rr_audio_graph_link_count(const rr_audio_graph_t *graph);
#endif

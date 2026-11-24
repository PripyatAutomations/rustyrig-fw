//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/audio.graph.h>
#include <string.h>
typedef struct {
   char id[64];
   rr_ag_node_type_t type;
   rr_ag_health_t health;
   char route;
} rr_ag_node_t;
typedef struct {
   guint src, dst;
   rr_ag_link_type_t type;
   guint8 channels; /* source mask for existing graph consumers */
   guint8 src_channels,dst_channels,control_pin;
} rr_ag_edge_t;
struct rr_audio_graph {
   GArray *nodes, *edges;
};
static gint lookup(const rr_audio_graph_t *g, const char *id) {
   if (!g || !id) {
      return -1;
   }

   for (guint i = 0 ; i < g->nodes->len ; i++) {
      const rr_ag_node_t *n = &g_array_index(g->nodes, rr_ag_node_t, i);

      if (!strcmp(n->id, id)) {
         return (gint) i;
      }
   }

   return -1;
}

const char *rr_audio_graph_listen_bus(unsigned downstream_rate_hz,
                                      const char *requested_mixer) {
   if (!requested_mixer) return NULL;
   const gboolean hq = downstream_rate_hz > 16000;
   if (hq)
      return strcmp(requested_mixer, "hq.mix") ? NULL : "hq.mix";
   if (!strcmp(requested_mixer, "rx.mix") ||
       !strcmp(requested_mixer, "hq.mix"))
      return "rx.mix"; /* existing rx16.resample -> HQ output */
   return NULL;
}

rr_audio_graph_t *rr_audio_graph_new(void) {
   rr_audio_graph_t *g = g_new0(rr_audio_graph_t, 1);
   g->nodes = g_array_new(FALSE, FALSE, sizeof(rr_ag_node_t));
   g->edges = g_array_new(FALSE, FALSE, sizeof(rr_ag_edge_t));

   return g;
}

void rr_audio_graph_free(rr_audio_graph_t *g) {
   if (!g) {
      return;
   }
   g_array_unref(g->nodes);
   g_array_unref(g->edges);
   g_free(g);
}

bool rr_audio_graph_add_node(rr_audio_graph_t *g, const char *id, rr_ag_node_type_t type) {
   if (!g || !id || !*id || strlen(id) >= 64 || type > RR_AG_SINK || lookup(g, id) >= 0 || g->nodes->len >= 256) {
      return false;
   }
   rr_ag_node_t n = {
      .type = type, .health = RR_AG_STOPPED
   };
   g_strlcpy(n.id, id, sizeof(n.id));
   g_array_append_val(g->nodes, n);

   return true;
}

/* A bypass edge is a conditional alternative, not a feedback connection. Refuse cycles in the union of normal and fallback edges. */
static bool graph_reaches(const rr_audio_graph_t *g, guint start, guint target) {
   bool visited[256] = {
      0
   };
   guint queue[256], head = 0, tail = 0;
   queue[tail++] = start;
   visited[start] = true;
   while (head < tail) {
      guint current = queue[head++];

      if (current == target) {
         return true;
      }

      for (guint i = 0 ; i < g->edges->len ; i++) {
         const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);

         if (e->src == current && !visited[e->dst]) {
            visited[e->dst] = true;
            queue[tail++] = e->dst;
         }
      }
   }
   return false;
}

bool rr_audio_graph_add_port_link(rr_audio_graph_t *g,const char *src,const char *dst,
                                  rr_ag_link_type_t type,unsigned src_channels,
                                  unsigned dst_channels) {
   gint a = lookup(g, src), b = lookup(g, dst);

   if (a < 0 || b < 0 || a == b || type > RR_AG_CONTROL || !src_channels || !dst_channels ||
      (src_channels & ~RR_AG_CHANNEL_STEREO) ||
      (dst_channels & ~RR_AG_CHANNEL_STEREO) || g->edges->len >= 512) {
      return false;
   }
   const rr_ag_node_t *from = &g_array_index(g->nodes, rr_ag_node_t, (guint) a);
   const rr_ag_node_t *to = &g_array_index(g->nodes, rr_ag_node_t, (guint) b);

   if (from->type == RR_AG_SINK || to->type == RR_AG_SOURCE || graph_reaches(g, (guint) b, (guint) a)) {
      return false;
   }

   /* Stereo processors can take different L/R sources, but never two feeds to the same channel. Mixers remain the only freely mixed nodes. Coalesce disjoint
    * channel links from the same source for rendering. */
   for (guint i = 0 ; i < g->edges->len ; i++) {
      rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);

      if (e->dst != (guint) b || e->type != type) {
         continue;
      }
      gboolean stereo_processor = to->type == RR_AG_PROCESSOR && g_str_has_prefix(to->id, "proc.lv2.");
      /* rig.sink exposes a logical modem audio patch alongside its real TX
       * transport feed. Both ultimately use the same encoder bus, so this
       * metadata edge is not a second physical audio input to the rig. */
      gboolean direct_modem_tx = type == RR_AG_NORMAL && !strcmp(to->id,"rig.sink") &&
         g_str_has_prefix(from->id,"modem") && g_str_has_suffix(from->id,".tx");

      /* rig.sink is a *view* over the one selected TX encoder bus:
       * tx.transport is the real media feed, while modem audio represents
       * separate TX mixer ingress. Allow these distinct edges to coexist,
       * but never relax the single-feed rule for ordinary sinks. */
      const rr_ag_node_t *prior =
         &g_array_index(g->nodes, rr_ag_node_t, e->src);
      gboolean transport_feed = type == RR_AG_NORMAL &&
         !strcmp(to->id, "rig.sink") && !strcmp(from->id, "tx.transport");
      gboolean prior_transport = !strcmp(prior->id, "tx.transport");
      gboolean prior_modem = g_str_has_prefix(prior->id, "modem") &&
         g_str_has_suffix(prior->id, ".tx");
      gboolean shared_tx_view = (direct_modem_tx &&
         (prior_transport || prior_modem)) ||
         (transport_feed && prior_modem);

      if (to->type != RR_AG_MIXER && !stereo_processor &&
          !shared_tx_view) {
         return false;
      }

      if (stereo_processor && (e->dst_channels & dst_channels)) {
         return false;
      }

      if (e->src == (guint) a && e->src_channels==e->dst_channels &&
          src_channels==dst_channels) {
         if (e->channels & src_channels) {
            return false;
         }
         e->channels |= (guint8) src_channels;
         e->src_channels |= (guint8) src_channels;
         e->dst_channels |= (guint8) dst_channels;

         return true;
      }
   }

   rr_ag_edge_t e = {
      .src = (guint) a, .dst = (guint) b, .type = type,
      .channels = (guint8) src_channels,
      .src_channels = (guint8) src_channels,
      .dst_channels = (guint8) dst_channels
   };
   g_array_append_val(g->edges, e);

   return true;
}

bool rr_audio_graph_add_numeric_link(rr_audio_graph_t *g,const char *source,
                                     const char *oscillator,unsigned input) {
   gint a=lookup(g,source),b=lookup(g,oscillator);
   if(a<0 || b<0 || a==b || input>2 || g->edges->len>=512 ||
      !g_str_has_prefix(source,"control.") ||
      !g_str_has_prefix(oscillator,"osc.") ||
      g_array_index(g->nodes,rr_ag_node_t,(guint)a).type!=RR_AG_SOURCE ||
      g_array_index(g->nodes,rr_ag_node_t,(guint)b).type!=RR_AG_SOURCE)
      return false;
   for(guint i=0;i<g->edges->len;i++) {
      const rr_ag_edge_t *edge=&g_array_index(g->edges,rr_ag_edge_t,i);
      if(edge->dst==(guint)b && edge->type==RR_AG_NUMBER &&
         edge->control_pin==input)return false;
   }
   rr_ag_edge_t edge={.src=(guint)a,.dst=(guint)b,.type=RR_AG_NUMBER,
                       .control_pin=(guint8)input,
                       .channels=RR_AG_CHANNEL_LEFT,
                       .src_channels=RR_AG_CHANNEL_LEFT,
                       .dst_channels=RR_AG_CHANNEL_LEFT};
   g_array_append_val(g->edges,edge);
   return true;
}

bool rr_audio_graph_add_channel_link(rr_audio_graph_t *g,const char *src,
                                     const char *dst,rr_ag_link_type_t type,
                                     unsigned channels) {
   return rr_audio_graph_add_port_link(g,src,dst,type,channels,channels);
}

bool rr_audio_graph_add_link(rr_audio_graph_t *g, const char *src, const char *dst, rr_ag_link_type_t type) {
   return rr_audio_graph_add_channel_link(g, src, dst, type, RR_AG_CHANNEL_STEREO);
}

bool rr_audio_graph_set_health(rr_audio_graph_t *g, const char *id, rr_ag_health_t health) {
   gint index = lookup(g, id);

   if (index < 0 || health > RR_AG_FAILED) {
      return false;
   }
   g_array_index(g->nodes, rr_ag_node_t, (guint) index).health = health;

   return true;
}

const char *rr_audio_graph_input(const rr_audio_graph_t *g, const char *dst) {
   gint index = lookup(g, dst);

   if (index < 0) {
      return NULL;
   }

   if (g_array_index(g->nodes, rr_ag_node_t, (guint) index).type == RR_AG_MIXER) {
      return NULL;              /* A mixer consumes multiple inputs; no single arbitration. */
   }
   const rr_ag_node_t *normal = NULL, *fallback = NULL;

   for (guint i = 0 ; i < g->edges->len ; i++) {
      const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);

      if (e->dst != (guint) index) {
         continue;
      }
      const rr_ag_node_t *source = &g_array_index(g->nodes, rr_ag_node_t, e->src);

      if (e->type == RR_AG_NORMAL) {
         normal = source;
      } else {
         fallback = source;
      }
   }

   if (normal && normal->health != RR_AG_FAILED) {
      return normal->id;
   }

   if (normal && normal->health == RR_AG_FAILED && fallback && fallback->health == RR_AG_HEALTHY) {
      return fallback->id;
   }

   return NULL;
}

guint rr_audio_graph_node_count(const rr_audio_graph_t *g) {
   return g ? g->nodes->len : 0;
}

guint rr_audio_graph_link_count(const rr_audio_graph_t *g) {
   return g ? g->edges->len : 0;
}

bool rr_audio_graph_snapshot(const rr_audio_graph_t *g, rr_audio_graph_snapshot_t *out) {
   if (!g || !out || g->nodes->len > RR_AUDIO_GRAPH_MAX_NODES || g->edges->len > RR_AUDIO_GRAPH_MAX_LINKS) {
      return false;
   }
   memset(out, 0, sizeof(*out));
   out->node_count = g->nodes->len;
   out->link_count = g->edges->len;

   for (guint i = 0 ; i < g->nodes->len ; i++) {
      const rr_ag_node_t *n = &g_array_index(g->nodes, rr_ag_node_t, i);
      rr_ag_view_node_t *view = &out->nodes[i];
      g_strlcpy(view->id, n->id, sizeof(view->id));
      view->type = n->type;
      view->health = n->health;
      view->route = n->route;
   }

   for (guint i = 0 ; i < g->edges->len ; i++) {
      const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);
      out->links[i] = (rr_ag_view_link_t) {
         .src = e->src, .dst = e->dst, .kind = e->type,
         .channels = e->src_channels, .src_channels=e->src_channels,
         .dst_channels=e->dst_channels,.control_pin=e->control_pin
      };
   }

   return true;
}

bool rr_audio_graph_set_route(rr_audio_graph_t *g, const char *id, char route) {
   gint index = lookup(g, id);

   if (index < 0 || !strchr("LRSM", route)) {
      return false;
   }
   g_array_index(g->nodes, rr_ag_node_t, (guint) index).route = route;

   return true;
}

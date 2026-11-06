#include "audio.graph.h"
#include <string.h>
typedef struct {
   char id[64];
   rr_ag_node_type_t type;
   rr_ag_health_t health;
} rr_ag_node_t;
typedef struct {
   guint src, dst;
   rr_ag_link_type_t type;
} rr_ag_edge_t;
struct rr_audio_graph {
   GArray *nodes, *edges;
};
static gint lookup(const rr_audio_graph_t *g, const char *id) {
   if (!g || !id) return -1;
   for (guint i = 0; i < g->nodes->len; i++) {
      const rr_ag_node_t *n = &g_array_index(g->nodes, rr_ag_node_t, i);
      if (!strcmp(n->id, id)) return (gint)i;
   }
   return -1;
}
rr_audio_graph_t *rr_audio_graph_new(void) {
   rr_audio_graph_t *g = g_new0(rr_audio_graph_t, 1);
   g->nodes = g_array_new(FALSE, FALSE, sizeof(rr_ag_node_t));
   g->edges = g_array_new(FALSE, FALSE, sizeof(rr_ag_edge_t));
   return g;
}
void rr_audio_graph_free(rr_audio_graph_t *g) {
   if (!g) return;
   g_array_unref(g->nodes);
   g_array_unref(g->edges);
   g_free(g);
}
bool rr_audio_graph_add_node(rr_audio_graph_t *g, const char *id, rr_ag_node_type_t type) {
   if (!g || !id || !*id || strlen(id) >= 64 ||
       type > RR_AG_SINK || lookup(g, id) >= 0 || g->nodes->len >= 256)
      return false;
   rr_ag_node_t n = { .type = type, .health = RR_AG_STOPPED };
   g_strlcpy(n.id, id, sizeof(n.id));
   g_array_append_val(g->nodes, n);
   return true;
}
/* A bypass edge is a conditional alternative, not a feedback connection.
 * Refuse cycles in the union of normal and fallback edges. */
static bool graph_reaches(const rr_audio_graph_t *g, guint start, guint target) {
   bool visited[256] = {0};
   guint queue[256], head = 0, tail = 0;
   queue[tail++] = start;
   visited[start] = true;
   while (head < tail) {
      guint current = queue[head++];
      if (current == target) return true;
      for (guint i = 0; i < g->edges->len; i++) {
         const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);
         if (e->src == current && !visited[e->dst]) {
            visited[e->dst] = true;
            queue[tail++] = e->dst;
         }
      }
   }
   return false;
}
bool rr_audio_graph_add_link(rr_audio_graph_t *g, const char *src, const char *dst, rr_ag_link_type_t type) {
   gint a = lookup(g, src), b = lookup(g, dst);
   if (a < 0 || b < 0 || a == b || type > RR_AG_BYPASS || g->edges->len >= 512)
      return false;
   const rr_ag_node_t *from = &g_array_index(g->nodes, rr_ag_node_t, (guint)a);
   const rr_ag_node_t *to = &g_array_index(g->nodes, rr_ag_node_t, (guint)b);
   if (from->type == RR_AG_SINK || to->type == RR_AG_SOURCE ||
       graph_reaches(g, (guint)b, (guint)a))
      return false;
   /* Inputs have separate normal and bypass arbitration. No accidental mixing
    * of two feeds into a single-input processor. Mixers are explicit nodes. */
   for (guint i = 0; i < g->edges->len; i++) {
      const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);
      if (e->src == (guint)a && e->dst == (guint)b && e->type == type)
         return false;
      if (to->type != RR_AG_MIXER && e->dst == (guint)b && e->type == type)
         return false;
   }
   rr_ag_edge_t e = { .src = (guint)a, .dst = (guint)b, .type = type };
   g_array_append_val(g->edges, e);
   return true;
}
bool rr_audio_graph_set_health(rr_audio_graph_t *g, const char *id, rr_ag_health_t health) {
   gint index = lookup(g, id);
   if (index < 0 || health > RR_AG_FAILED) return false;
   g_array_index(g->nodes, rr_ag_node_t, (guint)index).health = health;
   return true;
}
const char *rr_audio_graph_input(const rr_audio_graph_t *g, const char *dst) {
   gint index = lookup(g, dst);
   if (index < 0) return NULL;
   if (g_array_index(g->nodes, rr_ag_node_t, (guint)index).type == RR_AG_MIXER)
      return NULL; /* A mixer consumes multiple inputs; no single arbitration. */
   const rr_ag_node_t *normal = NULL, *fallback = NULL;
   for (guint i = 0; i < g->edges->len; i++) {
      const rr_ag_edge_t *e = &g_array_index(g->edges, rr_ag_edge_t, i);
      if (e->dst != (guint)index) continue;
      const rr_ag_node_t *source = &g_array_index(g->nodes, rr_ag_node_t, e->src);
      if (e->type == RR_AG_NORMAL) normal = source;
      else fallback = source;
   }
   if (normal && normal->health != RR_AG_FAILED)
      return normal->id;
   if (normal && normal->health == RR_AG_FAILED && fallback &&
       fallback->health == RR_AG_HEALTHY)
      return fallback->id;
   return NULL;
}
guint rr_audio_graph_node_count(const rr_audio_graph_t *g) { return g ? g->nodes->len : 0; }
guint rr_audio_graph_link_count(const rr_audio_graph_t *g) { return g ? g->edges->len : 0; }

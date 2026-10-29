//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Bounded mesh Dijkstra; pure route computation, no forwarding side effects.
#include <rrserver/link.mesh.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct rr_mesh {
   char id[RR_MESH_MAX_NODES][37];
   uint32_t edge[RR_MESH_MAX_NODES][RR_MESH_MAX_NODES];
   size_t count;
   const rr_mesh_router_t *router;
   void *router_context;
};
static int index_of(const rr_mesh_t *m, const char *id) {
   if (!id || !*id) {
      return -1;
   }

   for (size_t i = 0 ; i < m->count ; ++i) {
      if (!strcmp(id, m->id[i])) {
         return (int)i;
      }
   }

   return -1;
}
static bool valid(const char *id) {
   return id && strlen(id) == 36;
}
static int index_add(rr_mesh_t *m, const char *id) {
   int n = index_of(m, id);

   if (n >= 0) {
      return n;
   }

   if (!valid(id) || m->count == RR_MESH_MAX_NODES) {
      return -1;
   }
   n = (int)m->count++;
   strcpy(m->id[n], id);

   return n;
}
rr_mesh_t *rr_mesh_new(const char *local) {
   if (!valid(local)) {
      return NULL;
   }
   rr_mesh_t *m = calloc(1, sizeof(*m));

   if (m) {
      index_add(m, local);
   }

   return m;
}
void rr_mesh_free(rr_mesh_t *m) {
   free(m);
}
bool rr_mesh_edge(rr_mesh_t *m, const char *a, const char *b, uint32_t cost) {
   if (!m || !cost || !valid(a) || !valid(b) || !strcmp(a, b)) {
      return false;
   }

   if (index_of(m, a) < 0 && m->count == RR_MESH_MAX_NODES) {
      return false;
   }

   if (index_of(m, b) < 0 && m->count + (index_of(m, a) < 0) >= RR_MESH_MAX_NODES) {
      return false;
   }
   int x = index_add(m, a), y = index_add(m, b);

   if (x < 0 || y < 0) {
      return false;
   }
   m->edge[x][y] = m->edge[y][x] = cost;

   return true;
}
bool rr_mesh_remove_edge(rr_mesh_t *m, const char *a, const char *b) {
   int x = m ? index_of(m, a) : -1, y = m ? index_of(m, b) : -1;

   if (x < 0 || y < 0 || !m->edge[x][y]) {
      return false;
   }
   m->edge[x][y] = m->edge[y][x] = 0;

   return true;
}
static bool dijkstra_excluding(const rr_mesh_t *m, const char *destination, const bool excluded[RR_MESH_MAX_NODES], rr_mesh_route_t *out) {
   if (!m || !out) {
      return false;
   }
   int target = index_of(m, destination);

   if (target <= 0) {
      return false;
   }
   uint64_t dist[RR_MESH_MAX_NODES];
   unsigned hops[RR_MESH_MAX_NODES];
   int first[RR_MESH_MAX_NODES];
   bool visited[RR_MESH_MAX_NODES] = {
      0
   };

   for (size_t i = 0 ; i < m->count ; ++i) {
      dist[i] = UINT64_MAX;
      hops[i] = UINT_MAX;
      first[i] = -1;
   }

   dist[0] = 0;
   hops[0] = 0;

   for (size_t step = 0 ; step < m->count ; ++step) {
      int u = -1;

      for (size_t i = 0 ; i < m->count ; ++i) {
         if (!visited[i] && dist[i] != UINT64_MAX && (u < 0 || dist[i] < dist[u] || (dist[i] == dist[u] && strcmp(m->id[i], m->id[u]) < 0))) {
            u = (int)i;
         }
      }

      if (u < 0) {
         break;
      }
      visited[u] = true;

      for (size_t v = 1 ; v < m->count ; ++v) {
         uint32_t weight = m->edge[u][v];

         if (!weight || visited[v] || (u == 0 && excluded && excluded[v]) || dist[u] > UINT64_MAX - weight) {
            continue;
         }
         uint64_t candidate = dist[u] + weight;
         int hop = u == 0 ? (int)v : first[u];

         if (hop < 0) {
            continue;
         }
         unsigned nh = hops[u] + 1;

         if (candidate < dist[v] || (candidate == dist[v] && (nh < hops[v] || (nh == hops[v] && (first[v] < 0 || strcmp(m->id[hop], m->id[first[v]]) < 0))))) {
            dist[v] = candidate;
            hops[v] = nh;
            first[v] = hop;
         }
      }
   }

   if (first[target] < 0) {
      return false;
   }
   strcpy(out->next_hop, m->id[first[target]]);
   out->cost = dist[target];
   out->hops = hops[target];

   return true;
}

static bool route_dijkstra(const rr_mesh_t *m, const char *destination, rr_mesh_route_t *out, void *context) {
   (void)context;

   return dijkstra_excluding(m, destination, NULL, out);
}

bool rr_mesh_route_avoiding_many(const rr_mesh_t *m, const char *destination, const char *const *excluded_hops, size_t excluded_count, rr_mesh_route_t *out) {
   if (!m || !out || !destination || (excluded_count && !excluded_hops) || excluded_count > RR_MESH_MAX_NODES) {
      return false;
   }
   bool excluded[RR_MESH_MAX_NODES] = {
      0
   };

   for (size_t i = 0 ; i < excluded_count ; ++i) {
      int n = index_of(m, excluded_hops[i]);

      if (n <= 0 || !m->edge[0][n]) {
         return false;
      }
      excluded[n] = true;
   }

   rr_mesh_route_t proposal = {
      0
   };

   if (!dijkstra_excluding(m, destination, excluded, &proposal) || !rr_mesh_edge_cost(m, m->id[0], proposal.next_hop)) {
      return false;
   }
   *out = proposal;

   return true;
}

bool rr_mesh_route_avoiding(const rr_mesh_t *m, const char *destination, const char *excluded_next_hop, rr_mesh_route_t *out) {
   if (!excluded_next_hop) {
      return false;
   }
   const char *excluded[] = {
      excluded_next_hop
   };

   return rr_mesh_route_avoiding_many(m, destination, excluded, 1, out);
}

const rr_mesh_router_t rr_mesh_router_dijkstra = {
   .abi_version = RR_MESH_ROUTER_ABI,
   .name = "dijkstra",
   .route = route_dijkstra
};
bool rr_mesh_set_router(rr_mesh_t *m, const rr_mesh_router_t *router, void *context) {
   if (!m) {
      return false;
   }

   if (router && (router->abi_version != RR_MESH_ROUTER_ABI || !router->name || !router->route)) {
      return false;
   }
   m->router = router;
   m->router_context = router ? context : NULL;

   return true;
}
size_t rr_mesh_node_count(const rr_mesh_t *m) {
   return m ? m->count : 0;
}
const char *rr_mesh_node_id(const rr_mesh_t *m, size_t index) {
   return m && index < m->count ? m->id[index] : NULL;
}
uint32_t rr_mesh_edge_cost(const rr_mesh_t *m, const char *a, const char *b) {
   int x = m ? index_of(m, a) : -1, y = m ? index_of(m, b) : -1;

   return x < 0 || y < 0 ? 0 : m->edge[x][y];
}
bool rr_mesh_route(const rr_mesh_t *m, const char *destination, rr_mesh_route_t *out) {
   if (!m || !out || !destination) {
      return false;
   }
   const rr_mesh_router_t *router = m->router ? m->router : &rr_mesh_router_dijkstra;
   rr_mesh_route_t proposed = {
      0
   };

   if (!router->route(m, destination, &proposed, m->router_context)) {
      return false;
   }

   /* A plugin can only select an existing direct neighbor; it cannot create transport adjacency or authorize an unverified peer. */
   if (!rr_mesh_edge_cost(m, m->id[0], proposed.next_hop) || !proposed.hops || !proposed.cost || !strcmp(destination, m->id[0])) {
      return false;
   }
   *out = proposed;

   return true;
}

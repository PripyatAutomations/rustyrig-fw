//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Mesh route computation; independent of the current tree transport.
#ifndef RRSERVER_LINK_MESH_H
#define RRSERVER_LINK_MESH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RR_MESH_MAX_NODES 256
typedef struct rr_mesh rr_mesh_t;
#define RR_MESH_ROUTER_ABI 1u
typedef struct {
   char next_hop[37];
   uint64_t cost;
   unsigned hops;
} rr_mesh_route_t;
/* Routing policy interface. Plugins receive a read-only topology and return proposals; the host validates the immediate hop before forwarding. Lifetime: policy
 * context must outlive the mesh or be unset first. */
typedef struct {
   uint32_t abi_version;
   const char *name;
   bool (*route)(const rr_mesh_t *mesh, const char *destination, rr_mesh_route_t *out, void *context);
} rr_mesh_router_t;
/* Built-in deterministic shortest-path policy; safe fallback. */
extern const rr_mesh_router_t rr_mesh_router_dijkstra;
/* NULL selects the built-in policy. Rejects ABI mismatch and incomplete providers. */
bool rr_mesh_set_router(rr_mesh_t *mesh, const rr_mesh_router_t *router, void *context);
/* Read-only topology accessors for future loadable routing modules. */
size_t rr_mesh_node_count(const rr_mesh_t *mesh);
const char *rr_mesh_node_id(const rr_mesh_t *mesh, size_t index);
uint32_t rr_mesh_edge_cost(const rr_mesh_t *mesh, const char *a, const char *b);

rr_mesh_t *rr_mesh_new(const char *local);
void rr_mesh_free(rr_mesh_t *mesh);
/* Undirected verified links; cost must be nonzero. No route is advertised by this module, and callers must independently authenticate adjacency. */
bool rr_mesh_edge(rr_mesh_t *mesh, const char *a, const char *b, uint32_t cost);
bool rr_mesh_remove_edge(rr_mesh_t *mesh, const char *a, const char *b);
bool rr_mesh_route(const rr_mesh_t *mesh, const char *destination, rr_mesh_route_t *route);
/* Find an alternate verified direct neighbor, excluding one failed first hop.
 * Does not send, acknowledge, retry, or authorize a control operation. */
/* Exclude all previously tried direct first hops. Failed validation leaves out untouched. */
bool rr_mesh_route_avoiding_many(const rr_mesh_t *mesh, const char *destination,
                                 const char *const *excluded_hops, size_t excluded_count,
                                 rr_mesh_route_t *route);
bool rr_mesh_route_avoiding(const rr_mesh_t *mesh, const char *destination,
                            const char *excluded_next_hop, rr_mesh_route_t *route);
#endif

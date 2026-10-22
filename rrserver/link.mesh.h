// Mesh route computation; independent of the current tree transport.
#ifndef RRSERVER_LINK_MESH_H
#define RRSERVER_LINK_MESH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RR_MESH_MAX_NODES 256
typedef struct rr_mesh rr_mesh_t;
typedef struct {
   char next_hop[37];
   uint64_t cost;
   unsigned hops;
} rr_mesh_route_t;
rr_mesh_t *rr_mesh_new(const char *local);
void rr_mesh_free(rr_mesh_t *mesh);
/* Undirected verified links; cost must be nonzero. No route is advertised by
 * this module, and callers must independently authenticate adjacency. */
bool rr_mesh_edge(rr_mesh_t *mesh, const char *a, const char *b, uint32_t cost);
bool rr_mesh_remove_edge(rr_mesh_t *mesh, const char *a, const char *b);
bool rr_mesh_route(const rr_mesh_t *mesh, const char *destination, rr_mesh_route_t *route);
#endif

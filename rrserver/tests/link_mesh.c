#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/link.mesh.h>
#define A "00000000-0000-4000-8000-000000000001"
#define B "00000000-0000-4000-8000-000000000002"
#define C "00000000-0000-4000-8000-000000000003"
#define D "00000000-0000-4000-8000-000000000004"
#define E "00000000-0000-4000-8000-000000000005"
int main(void) {
   rr_mesh_t *m = rr_mesh_new(A);
   rr_mesh_route_t r;
   assert(m);
   assert(rr_mesh_edge(m, A, B, 1));
   assert(rr_mesh_edge(m, B, D, 1));
   assert(rr_mesh_edge(m, A, C, 2));
   assert(rr_mesh_edge(m, A, E, 3));
   assert(rr_mesh_edge(m, E, D, 3));
   assert(rr_mesh_edge(m, C, D, 2));
   assert(rr_mesh_route(m, D, &r));
   assert(!strcmp(r.next_hop, B) && r.cost == 2 && r.hops == 2);
   assert(rr_mesh_route_avoiding(m, D, B, &r));
   assert(!strcmp(r.next_hop, C) && r.cost == 4 && r.hops == 2);
   const char *tried[] = {
      B, C
   };
   assert(rr_mesh_route_avoiding_many(m, D, tried, 2, &r));
   assert(!strcmp(r.next_hop, E) && r.cost == 6);
   const char *all[] = {
      B, C, E
   };
   assert(!rr_mesh_route_avoiding_many(m, D, all, 3, &r));
   assert(!rr_mesh_route_avoiding_many(m, D, NULL, 1, &r));
   const char *invalid[] = {
      B, "not-a-node"
   };
   assert(!rr_mesh_route_avoiding_many(m, D, invalid, 2, &r));
   assert(!rr_mesh_route_avoiding(m, D, C, NULL));
   assert(rr_mesh_route_avoiding(m, D, E, &r));
   assert(!strcmp(r.next_hop, B) && r.cost == 2);
   assert(!rr_mesh_route_avoiding(m, A, B, &r));
   assert(rr_mesh_remove_edge(m, A, C));
   assert(rr_mesh_route_avoiding(m, D, B, &r));
   assert(!strcmp(r.next_hop, E));
   assert(rr_mesh_edge(m, A, C, 2));
   assert(rr_mesh_edge(m, C, E, 1));
   assert(rr_mesh_edge(m, E, D, 1));
   assert(rr_mesh_route_avoiding(m, D, B, &r));
   assert(!strcmp(r.next_hop, C) && r.cost == 4);
   rr_mesh_free(m);
   puts("PASS: mesh shortest path and alternate first-hop failover");

   return 0;
}

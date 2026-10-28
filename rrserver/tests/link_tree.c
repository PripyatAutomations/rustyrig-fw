#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/link.tree.h>
#define A "00000000-0000-4000-8000-000000000001"
#define B "00000000-0000-4000-8000-000000000002"
#define C "00000000-0000-4000-8000-000000000003"
#define D "00000000-0000-4000-8000-000000000004"
#define E "00000000-0000-4000-8000-000000000005"
#define F "00000000-0000-4000-8000-000000000006"
#define G "00000000-0000-4000-8000-000000000007"
int main(void) {
   assert(!rr_link_tree_new("invalid", 8));
   assert(!rr_link_tree_new(A, 0));
   rr_link_tree_t *tree = rr_link_tree_new(A, 8);
   assert(tree);
   rr_link_node_t first[] = { {
                                 D, C, "d"
                              }, {
                                 B, NULL, "b"
                              }, {
                                 C, B, "c"
                              } };
   assert(rr_link_tree_attach(tree, B, first, 3) == RR_LINK_OK);
   assert(rr_link_tree_count(tree) == 4);
   assert(!strcmp(rr_link_tree_route(tree, D), B));
   assert(!rr_link_tree_route(tree, A));
   rr_link_node_t second[] = { {
                                  E, NULL, "e"
                               }, {
                                  F, E, "f"
                               } };
   assert(rr_link_tree_attach(tree, E, second, 2) == RR_LINK_OK);
   assert(!strcmp(rr_link_tree_route(tree, F), E));
   rr_link_node_t overlap[] = { {
                                   G, NULL, "g"
                                }, {
                                   D, G, "d"
                                } };
   assert(rr_link_tree_attach(tree, G, overlap, 2) == RR_LINK_OVERLAP);
   assert(rr_link_tree_count(tree) == 6 && !rr_link_tree_route(tree, G));
   rr_link_node_t self[] = { {
                                A, NULL, "a"
                             } };
   assert(rr_link_tree_attach(tree, A, self, 1) == RR_LINK_OVERLAP);
   rr_link_node_t cycle[] = { {
                                 G, NULL, "g"
                              }, {
                                 A, G, "a"
                              } };
   assert(rr_link_tree_attach(tree, G, cycle, 2) == RR_LINK_OVERLAP);
   assert(rr_link_tree_add(tree, E, G, D, "g") == RR_LINK_INVALID);
   assert(rr_link_tree_add(tree, C, G, C, "g") == RR_LINK_INVALID);
   assert(rr_link_tree_add(tree, B, G, D, "g") == RR_LINK_OK);
   assert(!strcmp(rr_link_tree_route(tree, G), B));
   assert(rr_link_tree_add(tree, E, G, F, "g") == RR_LINK_OVERLAP);
   assert(!rr_link_tree_remove(tree, E, C));
   assert(!rr_link_tree_remove(tree, B, A));
   assert(rr_link_tree_remove(tree, B, C));
   assert(!rr_link_tree_route(tree, C) && !rr_link_tree_route(tree, D) && !rr_link_tree_route(tree, G));
   assert(!strcmp(rr_link_tree_route(tree, B), B));
   assert(!strcmp(rr_link_tree_route(tree, F), E));
   assert(rr_link_tree_remove(tree, E, E));
   assert(!rr_link_tree_route(tree, E) && !rr_link_tree_route(tree, F));
   assert(rr_link_tree_count(tree) == 2);
   assert(rr_link_tree_remove(tree, B, B));
   rr_link_node_t malformed[] = { {
                                     B, NULL, "b"
                                  }, {
                                     C, D, "c"
                                  }, {
                                     D, C, "d"
                                  } };
   assert(rr_link_tree_attach(tree, B, malformed, 3) == RR_LINK_INVALID);
   malformed[1].parent = E;
   assert(rr_link_tree_attach(tree, B, malformed, 3) == RR_LINK_INVALID);
   malformed[1].parent = NULL;
   assert(rr_link_tree_attach(tree, B, malformed, 3) == RR_LINK_INVALID);
   rr_link_node_t duplicate[] = { {
                                     B, NULL, "b"
                                  }, {
                                     B, B, "b"
                                  } };
   assert(rr_link_tree_attach(tree, B, duplicate, 2) == RR_LINK_OVERLAP);
   assert(rr_link_tree_count(tree) == 1);
   rr_link_tree_free(tree);
   tree = rr_link_tree_new(A, 2);
   assert(rr_link_tree_attach(tree, B, first, 3) == RR_LINK_LIMIT);
   rr_link_node_t one = {
      B, NULL, "b"
   };
   assert(rr_link_tree_attach(tree, B, &one, 1) == RR_LINK_OK);
   assert(rr_link_tree_add(tree, B, C, B, "c") == RR_LINK_LIMIT);
   rr_link_node_t node;
   assert(rr_link_tree_node(tree, 1, &node));
   assert(!strcmp(node.server, B) && !strcmp(node.parent, A));
   assert(!rr_link_tree_node(tree, 2, &node));
   rr_link_tree_free(tree);
   puts("PASS: multi-peer tree routing, atomic overlap/cycle rejection, source ownership and branch withdrawal");
}

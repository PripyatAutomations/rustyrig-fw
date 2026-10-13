//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-link tree ownership and next-hop routing. No transport or UI state.
#ifndef RRSERVER_LINK_TREE_H
#define RRSERVER_LINK_TREE_H
#include <stdbool.h>
#include <stddef.h>

typedef struct rr_link_tree rr_link_tree_t;
typedef struct {
   const char *server;
   const char *parent;
} rr_link_node_t;
typedef enum {
   RR_LINK_OK, RR_LINK_INVALID, RR_LINK_OVERLAP, RR_LINK_LIMIT
} rr_link_result_t;

/* IDs are canonical UUIDs. Limit includes the local server. */
rr_link_tree_t *rr_link_tree_new(const char *local, size_t limit);
void rr_link_tree_free(rr_link_tree_t *tree);
size_t rr_link_tree_count(const rr_link_tree_t *tree);
bool rr_link_tree_node(const rr_link_tree_t *tree, size_t index, rr_link_node_t *node);
/* Admit an authenticated peer's complete component atomically. The peer's parent is NULL; every other parent must exist within the offered component. Admission
 * must finish before advertising this component to other peers. */
rr_link_result_t rr_link_tree_attach(rr_link_tree_t *tree, const char *peer, const rr_link_node_t *nodes, size_t count);
/* Incremental introductions may only extend the source peer's own branch. */
rr_link_result_t rr_link_tree_add(rr_link_tree_t *tree, const char *peer, const char *server, const char *parent);
/* NULL means unknown or local. Returned strings live until the next mutation. */
const char *rr_link_tree_route(const rr_link_tree_t *tree, const char *server);
/* Remove a source-owned subtree; removing the peer withdraws its entire link. */
bool rr_link_tree_remove(rr_link_tree_t *tree, const char *peer, const char *server);
#endif

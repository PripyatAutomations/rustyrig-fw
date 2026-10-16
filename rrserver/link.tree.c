//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-authoritative tree topology. A server is reachable through one peer.
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/objects.h>
#include <rrserver/link.tree.h>
struct link_node {
   char server[37], parent[37], peer[37], site[128];
};
struct rr_link_tree {
   struct link_node *nodes;
   size_t count, limit;
};

static bool valid_id(const char *id) {
   if (!rr_object_uuid_valid(id)) {
      return false;
   }

   /* One spelling per identity avoids case-sensitive duplicate routes. */
   for (const char *p = id ; *p ; p++) {
      if (*p >= 'A' && *p <= 'F') {
         return false;
      }
   }

   return true;
}
static bool valid_site(const char *site) {
   if (!site || !*site || strlen(site) >= 128) return false;
   for (const unsigned char *p = (const unsigned char *)site ; *p ; p++) {
      if (*p < 32 || *p == 127) return false;
   }
   return true;
}

bool rr_link_tree_set_local_site(rr_link_tree_t *tree, const char *site) {
   if (!tree || !valid_site(site)) return false;
   strcpy(tree->nodes[0].site, site);
   return true;
}
static struct link_node *find(const rr_link_tree_t *tree, const char *id) {
   if (tree && id) {
      for (size_t i = 0 ; i < tree->count ; i++) {
         if (!strcmp(tree->nodes[i].server, id)) {
            return &tree->nodes[i];
         }
      }
   }

   return NULL;
}

rr_link_tree_t *rr_link_tree_new(const char *local, size_t limit) {
   if (!valid_id(local) || !limit || limit > 256) {
      return NULL;
   }
   rr_link_tree_t *tree = calloc(1, sizeof(*tree));
   if (!tree) {
      return NULL;
   }
   tree->nodes = calloc(limit, sizeof(*tree->nodes));

   if (!tree->nodes) {
      free(tree);

      return NULL;
   }
   tree->count = 1;
   tree->limit = limit;
   strcpy(tree->nodes[0].server, local);

   return tree;
}

void rr_link_tree_free(rr_link_tree_t *tree) {
   if (tree) {
      free(tree->nodes);
      free(tree);
   }
}

size_t rr_link_tree_count(const rr_link_tree_t *tree) {
   return tree ? tree->count : 0;
}
bool rr_link_tree_node(const rr_link_tree_t *tree, size_t index, rr_link_node_t *node) {
   if (!tree || !node || index >= tree->count) {
      return false;
   }
   node->server = tree->nodes[index].server;
   node->parent = index ? tree->nodes[index].parent : NULL;
   node->site = tree->nodes[index].site;

   return true;
}

rr_link_result_t rr_link_tree_attach(rr_link_tree_t *tree, const char *peer, const rr_link_node_t *nodes, size_t count) {
   if (!tree || !valid_id(peer) || !nodes || !count) {
      return RR_LINK_INVALID;
   }
   if (count > tree->limit - tree->count) {
      return RR_LINK_LIMIT;
   }
   size_t roots = 0;

   for (size_t i = 0 ; i < count ; i++) {
      if (!valid_id(nodes[i].server) || !valid_site(nodes[i].site)) {
         return RR_LINK_INVALID;
      }

      if (find(tree, nodes[i].server)) {
         return RR_LINK_OVERLAP;
      }

      for (size_t j = 0 ; j < i ; j++) {
         if (!strcmp(nodes[i].server, nodes[j].server)) {
            return RR_LINK_OVERLAP;
         }
      }
      if (!strcmp(nodes[i].server, peer)) {
         if (nodes[i].parent) {
            return RR_LINK_INVALID;
         }
         roots++;
      } else if (!valid_id(nodes[i].parent)) {
         return RR_LINK_INVALID;
      }
   }

   if (roots != 1) {
      return RR_LINK_INVALID;
   }
   /* Every parent chain must reach the peer within count steps. This accepts unordered snapshots and rejects cycles, missing parents and extra roots. */
   for (size_t i = 0 ; i < count ; i++) {
      const char *id = nodes[i].server;
      size_t steps = 0;
      while (strcmp(id, peer)) {
         if (++steps > count) {
            return RR_LINK_INVALID;
         }
         size_t j;
         for (j = 0 ; j < count ; j++) {
            if (!strcmp(id, nodes[j].server)) {
               break;
            }
         }

         if (j == count || !nodes[j].parent) {
            return RR_LINK_INVALID;
         }
         id = nodes[j].parent;
      }
   }
   for (size_t i = 0 ; i < count ; i++) {
      struct link_node *node = &tree->nodes[tree->count++];
      strcpy(node->server, nodes[i].server);
      strcpy(node->site, nodes[i].site);
      strcpy(node->parent, nodes[i].parent ? nodes[i].parent : tree->nodes[0].server);
      strcpy(node->peer, peer);
   }

   return RR_LINK_OK;
}
rr_link_result_t rr_link_tree_add(rr_link_tree_t *tree, const char *peer, const char *server, const char *parent, const char *site) {
   if (!tree || !valid_id(peer) || !valid_id(server) || !valid_id(parent) || !valid_site(site)) {
      return RR_LINK_INVALID;
   }
   struct link_node *owner = find(tree, parent), *source = find(tree, peer);

   if (!source || strcmp(source->peer, peer) || strcmp(source->server, source->peer) || !owner || strcmp(owner->peer, peer)) {
      return RR_LINK_INVALID;
   }
   if (find(tree, server)) {
      return RR_LINK_OVERLAP;
   }

   if (tree->count == tree->limit) {
      return RR_LINK_LIMIT;
   }
   struct link_node *node = &tree->nodes[tree->count++];
   strcpy(node->server, server);
   strcpy(node->site, site);
   strcpy(node->parent, parent);
   strcpy(node->peer, peer);

   return RR_LINK_OK;
}

const char *rr_link_tree_route(const rr_link_tree_t *tree, const char *server) {
   struct link_node *node = find(tree, server);

   return node && node->peer[0] ? node->peer : NULL;
}
bool rr_link_tree_remove(rr_link_tree_t *tree, const char *peer, const char *server) {
   struct link_node *node = find(tree, server);

   if (!node || !peer || !node->peer[0] || strcmp(node->peer, peer)) {
      return false;
   }
   /* Mark before compacting so children retain their parents during traversal. */
   bool removed[256] = {
      false
   };
   removed[node - tree->nodes] = true;
   bool changed;
   do {
      changed = false;
      for (size_t i = 1 ; i < tree->count ; i++) {
         if (removed[i]) {
            continue;
         }
         struct link_node *parent = find(tree, tree->nodes[i].parent);

         if (parent && removed[parent - tree->nodes]) {
            removed[i] = changed = true;
         }
      }
   } while (changed);
   size_t kept = 0;

   for (size_t i = 0 ; i < tree->count ; i++) {
      if (!removed[i]) {
         tree->nodes[kept++] = tree->nodes[i];
      }
   }

   tree->count = kept;
   return true;
}

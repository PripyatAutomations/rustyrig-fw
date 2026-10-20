//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server inventory contributors. Wire contract: doc/resource-discovery.md.
#ifndef RRSERVER_DISCOVERY_H
#define RRSERVER_DISCOVERY_H
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/rig.registry.h>

/* Internal metadata collection; no client connection or resource access is granted. Contributors transfer each row to emit(), which owns and frees it. */
#define RR_INVENTORY_CATALOG_EVENT "server.inventory.catalog"
typedef struct {
   const char *scope;
   void (*emit)(dict *row, void *context);
   void *context;
} rr_inventory_catalog_t;
#define RR_INVENTORY_EVENT "server.inventory.collect"
/* Every registered stream has one tree parent. Unknown/non-radio producers, including video and file sources, belong to the site rather than disappearing. */
static inline const char *rr_inventory_media_parent(const struct rr_mediachan *channel, const rr_rig_registry_t *registry, const char *site) {
   if (channel->vfo_uuid[0] && rr_rig_registry_find_vfo_uuid(registry, channel->vfo_uuid)) {
      return channel->vfo_uuid;
   }

   if (channel->rig_uuid[0] && rr_rig_registry_find_uuid(registry, channel->rig_uuid)) {
      return channel->rig_uuid;
   }

   return site;
}
static inline dict *rr_inventory_row(const char *request, unsigned depth, const char *kind, const char *name, const char *uuid) {
   dict *d = dict_new();

   if (!d) {
      return NULL;
   }
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", "inventory-entry");
   dict_add(d, "request.id", request);
   dict_add_uint(d, "inventory.depth", depth);
   dict_add(d, "inventory.kind", kind);
   dict_add(d, "inventory.name", name ? name : "");

   if (uuid && *uuid) {
      dict_add(d, "inventory.uuid", uuid);
   }

   return d;
}
static inline void rr_inventory_send(rrconn_t *client, dict *d) {
   if (!d) {
      return;
   }
   ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}
#endif

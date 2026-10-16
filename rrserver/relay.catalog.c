//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Node/room/object discovery catalogs and merged user directories. */
#include "build_config.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#ifdef USE_MONGOOSE
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/media.health.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/wire.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <rrserver/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/database.h>
#include <rrserver/discovery.h>
#include <rrserver/relay.h>
#include <rrserver/media.direct.h>
#include <rrserver/supervision.h>
#include <rrserver/relay.internal.h>
extern struct GlobalState rig;
bool rr_relay_catalog_dirty;
size_t rr_relay_catalog_count;
struct catalog *rr_relay_catalogs;
struct directory *rr_relay_directories;

/* Replace a node user-directory snapshot with an independently owned clone. */
void rr_relay_directory_put(const char *node, dict *d) {
   unsigned count = dict_get_uint(d, "count", HTTP_MAX_USERS + 1);

   if (count > HTTP_MAX_USERS) {
      return;
   }

   for (unsigned i = 0 ; i < count ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "user.%u.name", i);
      const char *name = dict_get(d, key, NULL);

      if (!name || !*name || strlen(name) > HTTP_USER_LEN) {
         return;
      }
   }

   struct directory *entry = rr_relay_directories;
   unsigned entries = 0;

   for ( ; entry ; entry = entry->next) {
      entries++;

      if (!strcmp(entry->node, node)) {
         break;
      }
   }

   if (!entry) {
      if (entries >= 256) {
         return;
      }
      entry = calloc(1, sizeof(*entry));

      if (!entry) {
         return;
      }
      strcpy(entry->node, node);
      entry->next = rr_relay_directories;
      rr_relay_directories = entry;
   }
   dict_free(entry->users);
   entry->users = dict_clone(d);
}

/* Return an owned directory of authenticated local users and session counts. */
dict *rr_relay_local_users(void) {
   dict *d = dict_new();

   if (!d) {
      return NULL;
   }
   unsigned counts[HTTP_MAX_USERS] = {
      0
   }, n = 0;

   for (rrconn_t *c = http_client_list ; c ; c = c->next) {
      if (c->authenticated && c->is_ws && c->active && c->user && !client_has_flag(c, FLAG_VIDEO_SOURCE) && c->user->uid >= 0 && c->user->uid < HTTP_MAX_USERS)
      {
         counts[c->user->uid]++;
      }
   }

   for (unsigned i = 0 ; i < HTTP_MAX_USERS ; i++) {
      if (counts[i]) {
         char key[64];
         snprintf(key, sizeof(key), "user.%u.name", n);
         dict_add(d, key, http_users[i].name);
         snprintf(key, sizeof(key), "user.%u.sessions", n++);
         dict_add_uint(d, key, counts[i]);
      }
   }

   dict_add_uint(d, "count", n);

   return d;
}

/* Send merged local/remote user notices to the requesting client. */
void rr_relay_send_users(rrconn_t *client, const char *room) {
   // No credentials, addresses or peer-side privileges are imported here.
   rr_relay_users_poll(true);
   rrserver_relay_poll();
   unsigned count = 0;

   for (struct directory *entry = rr_relay_directories ; entry ; entry = entry->next) {
      struct catalog *site = NULL;

      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (!strcmp(c->key, entry->node)) {
            site = c;
         }
      }

      const char *alias = site ? dict_get(site->descriptor, "object.alias", entry->node) : cfg_get("station.name");

      for (unsigned i = 0 ; i < dict_get_uint(entry->users, "count", 0) ; i++) {
         char key[64], text[384];
         snprintf(key, sizeof(key), "user.%u.name", i);
         const char *name = dict_get(entry->users, key, "?");
         snprintf(key, sizeof(key), "user.%u.sessions", i);
         snprintf(text, sizeof(text), "%s@%s (%u sessions)", name, alias, dict_get_uint(entry->users, key, 0));
         dict *d = dict_new();

         if (!d) {
            continue;
         }
         dict_add(d, "msg.type", "talk");
         dict_add(d, "talk.cmd", "msg");
         dict_add(d, "talk.msg_type", "notice");
         dict_add(d, "talk.target", room ? room : ws_site_room());
         dict_add(d, "talk.from", "***SERVER***");
         dict_add(d, "talk.data", text);
         ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
         count++;
      }
   }

   dict *end = dict_new();

   if (end) {
      dict_add(end, "msg.type", "talk");
      dict_add(end, "talk.cmd", "msg");
      dict_add(end, "talk.msg_type", "notice");
      dict_add(end, "talk.target", room ? room : ws_site_room());
      dict_add(end, "talk.from", "***SERVER***");
      dict_add(end, "talk.data", count ? "End of users" : "No users online");
      ws_send_dict(NULL, client, end, WEBSOCKET_OP_TEXT);
      dict_free(end);
   }
}

/* Test whether an owner UUID identifies this server. */
bool rr_relay_local_owner(const char *node) {
   return !strcmp(node, rrserver_links_node());
}

/* Find a borrowed descriptor by UUID, room key or owner-scoped service key. */
struct catalog *rr_relay_find_catalog(const char *key) {
   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      if (!strcmp(c->key, key) || (!strncmp(key, "room:", 5) && !strcasecmp(c->key, key))) {
         return c;
      }
   }

   return NULL;
}

/* Find the borrowed catalog authority for a room. */
struct catalog *rr_relay_room_catalog(const char *room) {
   if (!room) {
      return NULL;
   }
   char key[160];
   snprintf(key, sizeof(key), "room:%s", room);

   return rr_relay_find_catalog(key);
}

/* Resolve the room targeted by a supported relay message. */
const char *rr_relay_message_room(dict *d) {
   const char *room = dict_get(d, "cat.room", dict_get(d, "talk.room", dict_get(d, "request.room", dict_get(d, "media.room", NULL))));

   if (!room) {
      const char *target = dict_get(d, "talk.target", NULL);

      if (target && (*target == '#' || *target == '&')) {
         room = target;
      }
   }

   if (!room) {
      struct catalog *c = rr_relay_find_catalog(dict_get(d, "target", ""));

      if (c) {
         room = dict_get(c->descriptor, "object.room", NULL);
      }
   }

   return room;
}

/* Check a node catalog for proof of station-scoped room ownership. */
static bool owns_station_room(const char *owner, const char *room) {
   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      const char *type = dict_get(c->descriptor, "object.type", "");

      if (!strcmp(c->owner, owner) && (!strcmp(type, "node") || !strcmp(type, "rig")) && !strcasecmp(dict_get(c->descriptor, "object.room", ""), room)) {
         return true;
      }
   }

   return false;
}

/* Validate room authority and retain an owned descriptor snapshot. */
void rr_relay_catalog_put(const char *owner, dict *d) {
   const char *uuid = dict_get(d, "object.uuid", NULL), *room = dict_get(d, "catalog.room", NULL);
   char key[160];

   if (uuid && !rr_relay_local_owner(owner)) {
      const char *type = dict_get(d, "object.type", ""), *parent = dict_get(d, "object.owner", "");

      if (!strcmp(type, "node")) {
         if (strcmp(uuid, owner)) {
            return;
         }
      } else if (!strcmp(type, "rig")) {
         if (strcmp(parent, owner)) {
            return;
         }
      } else if (!strcmp(type, "vfo")) {
         struct catalog *rig = rr_relay_find_catalog(parent);

         if (!rig || strcmp(rig->owner, owner) || strcmp(dict_get(rig->descriptor, "object.type", ""), "rig")) {
            return;
         }
      } else {
         return;
      }
   }

   const char *service = dict_get(d, "catalog.service", NULL);
   if (service) {
      const char *parent = dict_get(d, "catalog.parent", "");
      struct catalog *authority = rr_relay_find_catalog(parent);
      const char *kind = dict_get(d, "inventory.kind", "");
      if (uuid || strcmp(dict_get(d, "object.type", ""), "service") || !authority || strcmp(authority->owner, owner) ||
         (strcmp(dict_get(authority->descriptor, "object.type", ""), "node") &&
          strcmp(dict_get(authority->descriptor, "object.type", ""), "rig") && strcmp(dict_get(authority->descriptor, "object.type", ""), "vfo")) ||
         (strcmp(kind, "gps") && strcmp(kind, "serial") && strcmp(kind, "media") && strcmp(kind, "cat") && strcmp(kind,"share")) ||
         strlen(service) < 9 + strlen(owner) || strlen(service) >= sizeof(key) || strncmp(service, "service:", 8) ||
         strncmp(service + 8, owner, strlen(owner)) || service[8 + strlen(owner)] != ':') {
         return;
      }
      snprintf(key, sizeof(key), "%s", service);
   } else if (uuid && rr_object_uuid_valid(uuid)) {
      snprintf(key, sizeof(key), "%s", uuid);
   } else if (room && ws_room_name_valid(room)) {
      snprintf(key, sizeof(key), "room:%s", room);
   } else {
      return;
   }
   struct catalog *c = rr_relay_find_catalog(key);
   bool replace = c && strcmp(c->owner, owner);

   if (replace && (uuid || !room || rr_relay_local_owner(owner) || !owns_station_room(owner, room) ||
      owns_station_room(c->owner, room) || !strcasecmp(room, ws_site_room()) || rrserver_rig_room_configured(room))) {
      return;                   // Only proven station/rig authority may displace a stale room claim.
   }
   dict *copy = dict_clone(d);

   if (!copy) {
      return;
   }

   if (replace && rr_relay_local_owner(c->owner)) {
#ifdef USE_SQLITE
      bool exists, deleted;

      if (!db_room_status(masterdb, room, &exists, &deleted) || (exists && !db_room_delete(masterdb, room, "server-link", true, false))) {
         Log(LOG_WARN, "link", "Unable to remove stale local room metadata for %s", room);
         dict_free(copy);

         return;
      }
#endif

      /* Withdraw our old claim before advertising its authoritative owner. */
      for (struct catalog *peer = rr_relay_catalogs ; peer ; peer = peer->next) {
         const char *next = rrserver_links_route(peer->owner);

         if (next && !strcmp(next, peer->owner) && !strcmp(dict_get(peer->descriptor, "object.type", ""), "node")) {
            rr_relay_send_owned(next, c->owner, "catalog-remove", c->descriptor);
         }
      }

      ws_room_set_vfo_mask(room, 0);
      Log(LOG_INFO, "link", "Removed stale local room %s; authoritative owner is %s (chat history retained)", room, owner);
   }

   if (!c) {
      if (rr_relay_catalog_count == MAX_CATALOG) {
         dict_free(copy);

         return;
      }
      c = calloc(1, sizeof(*c));

      if (!c) {
         dict_free(copy);

         return;
      }
      snprintf(c->key, sizeof(c->key), "%s", key);
      snprintf(c->owner, sizeof(c->owner), "%s", owner);
      c->next = rr_relay_catalogs;
      rr_relay_catalogs = c;
      rr_relay_catalog_count++;
   }

   if (replace) {
      snprintf(c->key, sizeof(c->key), "%s", key);
      snprintf(c->owner, sizeof(c->owner), "%s", owner);
   }
   dict_free(c->descriptor);
   c->descriptor = copy;
   c->seen = true;

   if (!uuid && room && !rr_relay_local_owner(owner)) {
      rr_relay_rejoin_owned_room(owner, room);
   }
}

/* Build and store one local object descriptor. */
static void add_object(const char *uuid, const char *type, const char *owner, const char *alias, const char *room) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "msg.type", "object");
   dict_add(d, "object.cmd", "descriptor");
   dict_add(d, "object.uuid", uuid);
   dict_add(d, "object.type", type);

   if (owner) {
      dict_add(d, "object.owner", owner);
   }
   dict_add(d, "object.alias", alias);
   dict_add(d, "object.name", alias);
   dict_add(d, "object.room", room);
   dict_add(d, "object.lifecycle", "persistent");
   rr_relay_catalog_put(rrserver_links_node(), d);
   dict_free(d);
}

/* Service rows share local discovery contributors; they are metadata only.
 * Room/PTT/subscription and serial access remain checked by the serving node. */
static void collect_service(dict *row, void *context) {
   const char *parent = context;
   char key[160];
   char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, dict_get(row, "inventory.name", ""), -1);
   if (!hash) { dict_free(row); return; }
   snprintf(key, sizeof(key), "service:%s:%s:%.8s:%.32s", rrserver_links_node(), parent, dict_get(row, "inventory.kind", ""), hash);
   g_free(hash);
   dict_add(row, "object.type", "service");
   dict_add(row, "catalog.service", key);
   dict_add(row, "catalog.parent", parent);
   /* Subscription/join state belongs to the requester, never the publisher. */
   rr_relay_catalog_put(rrserver_links_node(), row);
   dict_free(row);
}

static void collect_services(const char *parent, const char *scope) {
   rr_inventory_catalog_t collect = { .scope = scope, .emit = collect_service, .context = (void *)parent };
   if (scope) event_emit_binary(RR_INVENTORY_CATALOG_EVENT, NULL, &collect, sizeof(collect));
   for (unsigned i = 0; i < MAX_MEDIA_CHANNELS; i++) {
      struct rr_mediachan *ch = &media_channels[i];
      const char *owner = rr_inventory_media_parent(ch,rig.rigs,rrserver_links_node());
      if (!ch->uuid[0] || strcmp(owner,parent)) continue;
      dict *row = rr_inventory_row("", 0, "media", ch->name, ch->uuid);
      if (!row) continue;
      dict_add(row, "inventory.room", ch->room);
      dict_add(row, "inventory.codec", ch->codec);
      dict_add(row, "inventory.direction", ch->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX");
      dict_add_uint(row, "inventory.subsystem", ch->subsystem);
      dict_add(row, "inventory.action", "/media subscribe|unsubscribe <uuid>");
      collect_service(row, (void *)parent);
   }
}

/* Add one registered VFO to the local discovery catalog. */
static bool collect_vfo(rr_server_vfo_t *vfo, void *user) {
   rr_server_rig_t *radio = user;
   add_object(rr_server_vfo_id(vfo), "vfo", rr_server_rig_id(radio), rr_server_vfo_alias(vfo), rr_rig_registry_room(rig.rigs, radio));

   collect_services(rr_server_vfo_id(vfo), NULL);
   return false;
}

/* Add a rig and its VFOs to the local discovery catalog. */
static bool collect_rig(rr_server_rig_t *radio, void *user) {
   (void)user;
   add_object(rr_server_rig_id(radio), "rig", rrserver_links_node(), rr_rig_registry_public_alias(rig.rigs, radio), rr_rig_registry_room(rig.rigs, radio));
   struct catalog *c = rr_relay_find_catalog(rr_server_rig_id(radio));

   if (c) {
      dict_add(c->descriptor, "object.backend", rr_server_rig_backend(radio)->type->name);
      dict_add(c->descriptor, "object.name", rr_server_rig_name(radio));
   }
   collect_services(rr_server_rig_id(radio), rr_rig_registry_public_alias(rig.rigs, radio));
   char cat[80];
   snprintf(cat, sizeof(cat), "%s.cat", rr_rig_registry_public_alias(rig.rigs, radio));
   dict *row = rr_inventory_row("", 0, "cat", cat, NULL);
   if (row) {
      dict_add(row, "inventory.room", rr_rig_registry_room(rig.rigs, radio));
      dict_add(row, "inventory.action", "/sercom attach <local-name> <rig-alias>.cat");
      collect_service(row, (void *)rr_server_rig_id(radio));
   }
   rr_server_vfo_foreach(radio, collect_vfo, radio);

   return false;
}

/* Refresh descriptors for locally owned nodes, objects and rooms. */
void rr_relay_collect_local(void) {
   if (!*rrserver_links_node() || !rig.rigs) {
      return;
   }
   add_object(rrserver_links_node(), "node", NULL, cfg_get("station.name"), ws_site_room());
   collect_services(rrserver_links_node(), "station");
   dict *site = dict_new();

   if (site) {
      dict_add(site, "catalog.room", ws_site_room());
      rr_relay_catalog_put(rrserver_links_node(), site);
      dict_free(site);
   }
   rr_rig_registry_foreach(rig.rigs, collect_rig, NULL);
#ifdef USE_SQLITE
   char *rooms = db_room_list(masterdb), *save = NULL;

   for (char *room = rooms ? strtok_r(rooms, " ", &save) : NULL ; room ; room = strtok_r(NULL, " ", &save)) {
      dict *d = dict_new();

      if (!d) {
         continue;
      }
      dict_add(d, "catalog.room", room);
      dict_add_ulong(d, "catalog.mask", ws_room_vfo_mask(room));
      rr_server_rig_t *radio = rrserver_rig_for_room(room);

      if (radio) {
         dict_add(d, "catalog.rig", rr_server_rig_id(radio));
      }
      rr_relay_catalog_put(rrserver_links_node(), d);
      dict_free(d);
   }

   free(rooms);
#endif
}

/* Send stored descriptors and user directories to a newly ready branch. */
void rr_relay_send_catalogs(const char *dest) {
   const char *types[] = {
      "node", "rig", "vfo", "room", "service"
   };

   for (unsigned i = 0 ; i < 5 ; i++) {
      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (strcmp(dict_get(c->descriptor, "object.type", "room"), types[i])) {
            continue;
         }
         // Forward the actual owning identity, not our intermediate identity.
         const char *route = rrserver_links_route(c->owner);

         if (route && !strcmp(route, dest)) {
            continue;
         }
         char *json = dict2json(c->descriptor);
         dict *d = dict_new();

         if (!json || !d) {
            free(json);
            dict_free(d);
            continue;
         }
         dict_add(d, "op", "link.application");
         dict_add(d, "node", c->owner);
         dict_add(d, "dest", dest);
         dict_add(d, "kind", "catalog");
         dict_add(d, "session", "");
         dict_add(d, "username", "");
         dict_add(d, "payload", json);
         rrserver_links_send(dest, d);
         dict_free(d);
         free(json);
      }
   }
}

/* Defer catalog rebuilding after a local model change. */
void rr_relay_dirty(const char *event, const char *data, rrconn_t *client, void *user) {
   rr_relay_catalog_dirty = true;
}

/* Reconcile dirty discovery and announce additions/removals to peers. */
void rr_relay_refresh_catalog(void) {
   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      if (rr_relay_local_owner(c->owner)) {
         c->seen = false;
      }
   }

   rr_relay_collect_local();
   struct catalog **p = &rr_relay_catalogs;
   while (*p) {
      struct catalog *c = *p;

      if (rr_relay_local_owner(c->owner) && !c->seen) {
         for (struct catalog *peer = rr_relay_catalogs ; peer ; peer = peer->next) {
            const char *next = rrserver_links_route(peer->owner);

            if (next && !strcmp(next, peer->owner) && !strcmp(dict_get(peer->descriptor, "object.type", ""), "node")) {
               rr_relay_send_owned(next, c->owner, "catalog-remove", c->descriptor);
            }
         }

         *p = c->next;
         dict_free(c->descriptor);
         free(c);
         rr_relay_catalog_count--;
      } else {
         p = &c->next;
      }
   }

   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      const char *next = rrserver_links_route(c->owner);

      if (next && !strcmp(next, c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
         rr_relay_send_catalogs(next);
      }
   }

   rr_relay_catalog_dirty = false;
}

/* Return an owned listing of rooms advertised by remote nodes. */
char *rrserver_relay_remote_rooms(void) {
   GString *text = g_string_new("");

   if (!text) {
      return NULL;
   }

   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);

      if (room && !rr_relay_local_owner(c->owner)) {
         if (text->len) {
            g_string_append_c(text, ' ');
         }
         g_string_append(text, room);
      }
   }

   return g_string_free(text, false);
}

/* Return the borrowed home-server alias, falling back to its UUID. */
const char *rrserver_relay_client_server(const rrconn_t *client) {
   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (&r->client != client) {
         continue;
      }

      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (!strcmp(c->owner, r->origin) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
            return dict_get(c->descriptor, "object.alias", r->origin);
         }
      }

      return r->origin;
   }

   return cfg_get("station.name");
}

/* Return an owned listing of discovered server nodes. */
char *rrserver_relay_servers(void) {
   GString *text = g_string_new(cfg_get("station.name"));

   if (!text) {
      return NULL;
   }

   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      if (!rr_relay_local_owner(c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
         g_string_append(text, " ↗");
         g_string_append(text, dict_get(c->descriptor, "object.alias", c->owner));
      }
   }

   return g_string_free(text, false);
}

/* Return an owned listing combining local and remote room names. */
char *rrserver_relay_rooms(const char *local) {
   GString *s = g_string_new(local ? local : "");

   if (!s) {
      return NULL;
   }

   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);

      if (room && !rr_relay_local_owner(c->owner)) {
         if (s->len) {
            g_string_append_c(s, ' ');
         }
         g_string_append(s, room);
      }
   }

   return g_string_free(s, false);
}

/* Send one catalog entry in the requested inventory format. */
static void inventory_entry(rrconn_t *client, const char *request, struct catalog *c, unsigned depth, const char *kind) {
   const char *room = dict_get(c->descriptor, "catalog.room", dict_get(c->descriptor, "object.room", ""));
   const char *name = dict_get(c->descriptor, "object.alias", room);
   dict *d = rr_inventory_row(request, depth, kind, name, dict_get(c->descriptor, "object.uuid", NULL));

   if (d) {
      dict_add(d, "inventory.room", room);
      dict_add(d, "inventory.state", ws_client_in_room(client, room) ? "joined" : "remote");
      const char *backend = dict_get(c->descriptor, "object.backend", NULL);

      if (backend) {
         dict_add(d, "inventory.backend", backend);
      }
      rr_inventory_send(client, d);
   }
}

/* Preserve contributor fields while rebuilding hierarchy and requester state. */
static void inventory_services(rrconn_t *client, const char *request, const char *owner, const char *parent, unsigned depth) {
   for (struct catalog *c = rr_relay_catalogs; c; c = c->next) {
      if (strcmp(c->owner, owner) || strcmp(dict_get(c->descriptor, "catalog.parent", ""), parent)) continue;
      const char *access = dict_get(c->descriptor, "inventory.access", NULL);
      if (access && (!client->user || !has_priv(client->user->uid, access))) continue;
      dict *row = dict_clone(c->descriptor);
      if (!row) continue;
      /* Relay catalog keys are private metadata, outside the client wire schema. */
      dict_del(row, "catalog.service");
      dict_del(row, "catalog.parent");
      dict_del(row, "object.type");
      dict_add(row, "object.cmd", "inventory-entry");
      dict_add(row, "request.id", request);
      dict_add_uint(row, "inventory.depth", depth);
      const char *kind = dict_get(row, "inventory.kind", "");
      if (!strcmp(kind, "cat") || !strcmp(kind, "serial")) {
         char state[160];
         snprintf(state, sizeof(state), "%s (owner-server-only)", dict_get(row, "inventory.state", "available"));
         dict_add(row, "inventory.state", state);
         dict_add(row, "inventory.action", "owner-server-only");
      }
      if (!strcmp(kind, "media")) {
         dict_add(row, "inventory.state", ws_client_in_room(client, dict_get(row, "inventory.room", "")) ? "available" : "join-room-first");
      }
      rr_inventory_send(client, row);
   }
}

/* Send remote inventory entries visible through the relay catalog. */
void rrserver_relay_inventory(rrconn_t *client, const char *request) {
   // Keep remote site/rig/VFO context explicit in both client discovery trees.
   for (struct catalog *site = rr_relay_catalogs ; site ; site = site->next) {
      if (rr_relay_local_owner(site->owner) || strcmp(dict_get(site->descriptor, "object.type", ""), "node")) {
         continue;
      }
      inventory_entry(client, request, site, 0, "site");
      inventory_services(client, request, site->owner, dict_get(site->descriptor, "object.uuid", ""), 1);

      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (!strcmp(c->owner, site->owner) && dict_get(c->descriptor, "catalog.room", NULL) && !dict_get(c->descriptor, "catalog.rig", NULL)) {
            inventory_entry(client, request, c, 1, "room");
         }
      }

      for (struct catalog *radio = rr_relay_catalogs ; radio ; radio = radio->next) {
         if (strcmp(radio->owner, site->owner) || strcmp(dict_get(radio->descriptor, "object.type", ""), "rig")) {
            continue;
         }
         inventory_entry(client, request, radio, 1, "rig");
         const char *id = dict_get(radio->descriptor, "object.uuid", "");
         inventory_services(client, request, site->owner, id, 2);

         for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
            if (strcmp(c->owner, site->owner)) {
               continue;
            }

            if (!strcmp(dict_get(c->descriptor, "object.owner", ""), id) && !strcmp(dict_get(c->descriptor, "object.type", ""), "vfo")) {
               inventory_entry(client, request, c, 2, "vfo");
               inventory_services(client, request, site->owner, dict_get(c->descriptor, "object.uuid", ""), 3);
            } else if (!strcmp(dict_get(c->descriptor, "catalog.rig", ""), id)) {
               inventory_entry(client, request, c, 2, "room");
            }
         }
      }
   }
}

/* Send discovered remote objects to an object-list requester. */
void rrserver_relay_objects(rrconn_t *client, const char *request) {
   // Parent descriptors must precede their children in both native/browser caches.
   const char *types[] = {
      "node", "rig", "vfo"
   };

   for (unsigned i = 0 ; i < 3 ; i++) {
      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (rr_relay_local_owner(c->owner) || strcmp(dict_get(c->descriptor, "object.type", ""), types[i])) {
            continue;
         }
         dict *d = dict_clone(c->descriptor);

         if (d) {
            dict_add(d, "request.id", request);
            rrserver_objects_remote(client, d);
            dict_free(d);
         }
      }
   }
}
#endif /* USE_MONGOOSE */

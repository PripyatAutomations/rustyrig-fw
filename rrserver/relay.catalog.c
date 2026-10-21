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
#include <rrserver/platform.controls.h>
#include <rrserver/gps.h>
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

#ifdef USE_SQLITE

   /* Discovery is a transient directory, not an account replica. Export enabled names, public contact email, session counts and role labels; never hashes. */
   if (masterdb) {
      sqlite3_stmt *stmt = NULL;

      if (sqlite3_prepare_v2(masterdb,
         "SELECT uid,name,COALESCE(permissions,''),COALESCE(email,''),COALESCE(real_name,''),COALESCE(phone,'') FROM users WHERE enabled=1 ORDER BY name COLLATE NOCASE",
         -1, &stmt, NULL) == SQLITE_OK) {
         while (n < HTTP_MAX_USERS && sqlite3_step(stmt) == SQLITE_ROW) {
            int uid = sqlite3_column_int(stmt, 0);
            const char *name = (const char *)sqlite3_column_text(stmt, 1);

            if (!name || !*name || strlen(name) > HTTP_USER_LEN) {
               continue;
            }
            char key[64];
            snprintf(key, sizeof(key), "user.%u.name", n);
            dict_add(d, key, name);
            snprintf(key, sizeof(key), "user.%u.sessions", n);
            dict_add_uint(d, key, uid >= 0 && uid < HTTP_MAX_USERS ? counts[uid] : 0);
            snprintf(key, sizeof(key), "user.%u.privileges", n++);
            const char *roles = (const char *)sqlite3_column_text(stmt, 2);
            dict_add(d, key, roles ? roles : "");
            const char *real = (const char *)sqlite3_column_text(stmt, 4);
            const char *phone = (const char *)sqlite3_column_text(stmt, 5);

            if (real && *real && strlen(real) <= 128) {
               snprintf(key, sizeof(key), "user.%u.real_name", n - 1);
               dict_add(d, key, real);
            }

            if (phone && *phone && strlen(phone) <= 64) {
               snprintf(key, sizeof(key), "user.%u.phone", n - 1);
               dict_add(d, key, phone);
            }
            const char *email = (const char *)sqlite3_column_text(stmt, 3);

            if (email && *email && strlen(email) <= 254 && !strpbrk(email, "\r\n")) {
               snprintf(key, sizeof(key), "user.%u.email", n - 1);
               dict_add(d, key, email);
            }
         }
      }
      sqlite3_finalize(stmt);
      unsigned p = 0;

      if (sqlite3_prepare_v2(masterdb, "SELECT name FROM privilege_sets ORDER BY name COLLATE NOCASE", -1, &stmt, NULL) == SQLITE_OK) {
         while (p < 256 && sqlite3_step(stmt) == SQLITE_ROW) {
            const char *name = (const char *)sqlite3_column_text(stmt, 0);

            if (!name || !*name || strlen(name) > 96) {
               continue;
            }
            char key[64];
            snprintf(key, sizeof(key), "privilege.%u.name", p++);
            dict_add(d, key, name);
         }
      }
      sqlite3_finalize(stmt);
      dict_add_uint(d, "privilege.count", p);
   }
#else

   for (unsigned i = 0 ; i < HTTP_MAX_USERS ; i++) {
      if (!counts[i]) {
         continue;
      }
      char key[64];
      snprintf(key, sizeof(key), "user.%u.name", n);
      dict_add(d, key, http_users[i].name);
      snprintf(key, sizeof(key), "user.%u.sessions", n++);
      dict_add_uint(d, key, counts[i]);
   }

#endif
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
         (strcmp(kind, "gps") && strcmp(kind, "serial") && strcmp(kind, "media") && strcmp(kind, "cat") && strcmp(kind, "share")) ||
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

   if (!strcmp(type, "node")) {
      dict_add(d, "object.description", cfg_get("station.description"));
      dict_add(d, "object.location", cfg_get("station.location"));
   }

   if (!strcmp(type, "node") || !strcmp(type, "rig")) {
      rrserver_gps_inventory_position(d, !strcmp(type, "node") ? "station" : alias);
   }
   rr_relay_catalog_put(rrserver_links_node(), d);
   dict_free(d);
}

/* Service rows share local discovery contributors; they are metadata only. Room/PTT/subscription and serial access remain checked by the serving node. */
static void collect_service(dict *row, void *context) {
   const char *parent = context;
   char key[160];
   char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, dict_get(row, "inventory.name", ""), -1);

   if (!hash) {
      dict_free(row);

      return;
   }
   /* Parent may itself be a service key (nested MENU); hash it to keep
    * catalog keys within the fixed-size wire/key limits. */
   char *parent_hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, parent, -1);
   snprintf(key, sizeof(key), "service:%s:%.24s:%.8s:%.32s", rrserver_links_node(), parent_hash ? parent_hash : "", dict_get(row, "inventory.kind", ""), hash);
   g_free(parent_hash);
   g_free(hash);
   dict_add(row, "object.type", "service");
   dict_add(row, "catalog.service", key);
   dict_add(row, "catalog.parent", parent);
   /* Subscription/join state belongs to the requester, never the publisher. */
   rr_relay_catalog_put(rrserver_links_node(), row);
   dict_free(row);
}

static void collect_services(const char *parent, const char *scope) {
   rr_inventory_catalog_t collect = {
      .scope = scope, .emit = collect_service, .context = (void *)parent
   };

   if (scope) {
      event_emit_binary(RR_INVENTORY_CATALOG_EVENT, NULL, &collect, sizeof(collect));
   }

   for (unsigned i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *ch = &media_channels[i];
      const char *owner = rr_inventory_media_parent(ch, rig.rigs, rrserver_links_node());

      if (!ch->uuid[0] || strcmp(owner, parent)) {
         continue;
      }
      dict *row = rr_inventory_row("", 0, "media", ch->name, ch->uuid);

      if (!row) {
         continue;
      }
      dict_add(row, "inventory.room", ch->room);
      dict_add(row, "inventory.codec", ch->codec);
      dict_add(row, "inventory.direction", ch->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX");
      dict_add_uint(row, "inventory.subsystem", ch->subsystem);
      dict_add(row, "inventory.action", "/media subscribe|unsubscribe <uuid>");
      collect_service(row, (void *)parent);
   }
}

/* Publish controls as node-owned service metadata, including on sBitx. Control writes remain at their owning server; relay inventory is read-only. */
static bool collect_node_control(const rr_platform_control_state_t *state, void *unused) {
   (void)unused;
   dict *row = rr_inventory_row("", 0, "control", state->label, NULL);

   if (!row) {
      return false;
   }
   /* inventory.control is not a declared wire field. Keep the stable key in an existing serialized field until the wire schema is extended. */
   dict_add(row, "inventory.service", state->name);
   dict_add(row, "inventory.description", state->name);
   dict_add(row, "inventory.state", "available");
   collect_service(row, (void *)rrserver_links_node());

   return false;
}

/* Mirror rig property descriptors and CAT menu metadata across relay hops.
 * These are inventory-only rows; CAT operations remain owner-local. */
typedef struct relay_rig_menu_ctx {
   const char *rig_id;
   const char *model;
   char menu_id[160];
} relay_rig_menu_ctx_t;

static bool collect_rig_property(const rr_property_descriptor_t *d, void *user) {
   const char *rig_id = user;
   dict *row = rr_inventory_row("", 2, "rig-control", d->name, NULL);
   if (row) {
      dict_add(row, "inventory.service", d->name);
      dict_add(row, "inventory.access", d->writable ? "read/write" : "read-only");
      dict_add(row, "inventory.description", "Rig property");
      collect_service(row, (void *)rig_id);
   }
   return false;
}
static void collect_rig_menu_item(const char *id, const char *label, void *user) {
   relay_rig_menu_ctx_t *ctx = user;
   if (!id || strlen(id) != 4) return;
   char name[128];
   snprintf(name, sizeof(name), "%c%c-%c%c %s", id[0], id[1], id[2], id[3], label);
   dict *row = rr_inventory_row("", 4, "rig-menu-item", name, NULL);
   if (row) {
      dict_add(row, "inventory.service", id);
      dict_add(row, "inventory.access", "read-only");
      dict_add(row, "inventory.description", "CAT menu; read on owning server");
      collect_service(row, ctx->menu_id);
   }
}
static void collect_rig_catalog(rr_server_rig_t *radio) {
   const char *id = rr_server_rig_id(radio);
   rr_rig_property_foreach(radio, collect_rig_property, (void *)id);
   rr_backend_t *backend = rr_server_rig_backend(radio);
   if (!backend || !backend->type->api->menu_foreach) return;
   const char *model = backend->type->api->model_name ?
      backend->type->api->model_name(backend) : NULL;
   if (!model || !strcmp(model, "Unknown Yaesu")) return;
   relay_rig_menu_ctx_t ctx = { .rig_id = id, .model = model };
   snprintf(ctx.menu_id, sizeof(ctx.menu_id), "%s.menu", id);
   dict *menu = rr_inventory_row("", 2, "rig-menu", "MENU", NULL);
   if (menu) {
      dict_add(menu, "inventory.description", model);
      collect_service(menu, (void *)id);
   }
   backend->type->api->menu_foreach(backend, collect_rig_menu_item, &ctx);
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
      /* A rig may be mobile independently of its site's fixed location. */
      const char *alias = rr_rig_registry_public_alias(rig.rigs, radio);
      char key[160];
      snprintf(key, sizeof(key), "rig:%s.mobile", alias);
      bool mobile = cfg_get_bool(key, false);
      snprintf(key, sizeof(key), "rig:%s.location", alias);
      const char *location = mobile ? "MOBILE" : cfg_get(key);
      dict_add(c->descriptor, "object.location", location && *location ? location : cfg_get("station.location"));
   }
   collect_services(rr_server_rig_id(radio), rr_rig_registry_public_alias(rig.rigs, radio));
   collect_rig_catalog(radio);
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
   rrserver_platform_control_foreach(collect_node_control, NULL);
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

/* Expose remote nodes as virtual share roots /@<node-uuid>. Access to individual shares is checked by the owner when opened. */
void rrserver_relay_file_nodes(rrconn_t *client, const char *request_id) {
   if (!client || !client->authenticated || !client->user) {
      return;
   }

   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      if (rr_relay_local_owner(c->owner) || !rrserver_links_route(c->owner) || strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
         continue;
      }
      dict *entry = dict_new();

      if (!entry) {
         continue;
      }
      char name[40];
      snprintf(name, sizeof(name), "@%s", c->owner);
      dict_add(entry, "msg.type", "file");
      dict_add(entry, "file.cmd", "entry");
      dict_add(entry, "request.id", request_id);
      dict_add(entry, "file.path", "/");
      dict_add(entry, "file.name", name);
      dict_add(entry, "file.site-name", dict_get(c->descriptor, "object.alias", c->owner));
      dict_add(entry, "file.kind", "directory");
      dict_add_ullong(entry, "file.size", 0);
      dict_add_bool(entry, "file.readable", true);
      dict_add_bool(entry, "file.writable", false);
      ws_send_dict(NULL, client, entry, WEBSOCKET_OP_TEXT);
      dict_free(entry);
   }
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
      const char *coordinates = dict_get(c->descriptor, "inventory.coordinates", NULL);

      if (coordinates) {
         dict_add(d, "inventory.coordinates", coordinates);
      }
      const char *description = dict_get(c->descriptor, "object.description", NULL);
      const char *location = dict_get(c->descriptor, "object.location", NULL);

      if (description) {
         dict_add(d, "inventory.description", description);
      }

      if (location) {
         dict_add(d, "inventory.location", location);
      }
      dict_add(d, "inventory.room", room);

      if (depth > 0) {
         dict_add(d, "inventory.parent", !strcmp(kind, "rig") || !strcmp(kind, "room") ? c->owner : dict_get(c->descriptor, "object.owner", c->owner));
      }
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
   for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
      if (strcmp(c->owner, owner) || strcmp(dict_get(c->descriptor, "catalog.parent", ""), parent)) {
         continue;
      }
      const char *access = dict_get(c->descriptor, "inventory.access", NULL);

      if (access && (!client->user || !has_priv(client->user->uid, access))) {
         continue;
      }
      dict *row = dict_clone(c->descriptor);

      if (!row) {
         continue;
      }
      /* Relay catalog keys are private metadata, outside the client wire schema. */
      dict_del(row, "catalog.service");
      dict_del(row, "catalog.parent");
      dict_del(row, "object.type");
      dict_add(row, "object.cmd", "inventory-entry");
      dict_add(row, "request.id", request);
      dict_add(row, "inventory.parent", parent);
      dict_add_uint(row, "inventory.depth", depth);
      const char *kind = dict_get(row, "inventory.kind", "");

      if (!strcmp(kind, "control")) {
         dict_add(row, "inventory.state", "owner-server-only");
      }

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

/* Directory snapshots are in-memory discovery data, never authorization. Reveal permission names only to local administrators/owners. */
static void inventory_directory_users(rrconn_t *client, const char *request, const char *owner, unsigned depth) {
   struct directory *directory = rr_relay_directories;
   while (directory && strcmp(directory->node, owner)) {
      directory = directory->next;
   }

   if (!directory || !directory->users) {
      return;
   }
   dict *folder = rr_inventory_row(request, depth, "user-directory", "Users", NULL);
   dict_add(folder, "inventory.description", "Public account directory");
   rr_inventory_send(client, folder);
   unsigned count = dict_get_uint(directory->users, "count", 0);

   if (count > HTTP_MAX_USERS) {
      count = HTTP_MAX_USERS;
   }

   for (unsigned i = 0 ; i < count ; i++) {
      char key[64];
      snprintf(key, sizeof(key), "user.%u.name", i);
      const char *name = dict_get(directory->users, key, "");

      if (!*name) {
         continue;
      }
      dict *row = rr_inventory_row(request, depth + 1, "user", name, NULL);
      snprintf(key, sizeof(key), "user.%u.sessions", i);
      unsigned sessions = dict_get_uint(directory->users, key, 0);
      /* Public profile: presence and the name already visible in /whois. Privileges are deliberately withheld from ordinary users. */
      char *description = g_strdup_printf("Account: %s\nPresence: %s (%u active session%s)", name, sessions ? "online" : "offline", sessions, sessions == 1 ?
         "" : "s");
      snprintf(key, sizeof(key), "user.%u.email", i);
      const char *email = dict_get(directory->users, key, "");

      if (*email) {
         char *with_contact = g_strdup_printf("%s\nEmail: %s", description, email);
         g_free(description);
         description = with_contact;
      }
      snprintf(key, sizeof(key), "user.%u.real_name", i);
      const char *real = dict_get(directory->users, key, "");

      if (*real) {
         char *extended = g_strdup_printf("%s\nReal name: %s", description, real);
         g_free(description);
         description = extended;
      }
      snprintf(key, sizeof(key), "user.%u.phone", i);
      const char *phone = dict_get(directory->users, key, "");

      if (*phone) {
         char *extended = g_strdup_printf("%s\nPhone: %s", description, phone);
         g_free(description);
         description = extended;
      }
      dict_add(row, "inventory.description", description);
      dict_add(row, "inventory.state", sessions ? "Online" : "Offline");

      if (client->user && has_priv(client->user->uid, "admin|owner")) {
         snprintf(key, sizeof(key), "user.%u.privileges", i);
         const char *roles = dict_get(directory->users, key, "");

         if (*roles) {
            char *private_details = g_strdup_printf("%s\\nServer roles: %s", description, roles);
            dict_add(row, "inventory.description", private_details);
            g_free(private_details);
         }
      }
      g_free(description);
      rr_inventory_send(client, row);
   }
}

/* Send remote inventory entries visible through the relay catalog. */
void rrserver_relay_inventory(rrconn_t *client, const char *request) {
   // Keep remote site/rig/VFO context explicit in both client discovery trees.
   rr_relay_users_poll(true);
   inventory_directory_users(client, request, rrserver_links_node(), 1);

   for (struct catalog *site = rr_relay_catalogs ; site ; site = site->next) {
      if (rr_relay_local_owner(site->owner) || strcmp(dict_get(site->descriptor, "object.type", ""), "node")) {
         continue;
      }
      inventory_entry(client, request, site, 0, "site");
      inventory_directory_users(client, request, site->owner, 1);
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

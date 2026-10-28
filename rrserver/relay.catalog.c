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
#include <unistd.h>
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
#include <rrserver/identity.h>
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
/* Local descriptor construction is a transaction: seal after all collectors finish. */
static bool catalog_collecting_local;
struct directory *rr_relay_directories;

#ifdef USE_PROFILING
/* Catalog work is owned by the rrserver event-loop thread; the existing five-minute server profile timer reads these counters on that same thread. There are no
 * per-message allocations, locks or clocks in normal builds. */
typedef struct {
   guint64 snapshots, prepared, queued, sent, aborted, superseded;
   guint64 prepared_bytes, sent_bytes, received_bytes;
   guint64 ack_ok, ack_partial, ack_timeout, ack_cancelled;
   guint64 ack_received, ack_accepted, ack_rejected;
   guint64 received, accepted, rejected;
   guint64 refreshes, delta_updates, delta_removes;
   guint64 build_us, build_max_us, drain_us, drain_max_us;
   guint64 ack_us, ack_max_us, apply_us, apply_max_us;
   guint64 refresh_us, refresh_max_us;
} catalog_profile_counts_t;

typedef struct {
   char node[37], label[192];
   guint64 snapshots, queued, ack_ok, ack_partial, timeouts, aborted;
   guint64 sent, accepted, rejected, last_ack_us, max_ack_us;
} catalog_profile_peer_t;

#define CATALOG_PROFILE_MAX_PEERS 256
static catalog_profile_counts_t catalog_profile_total;
static catalog_profile_counts_t catalog_profile_interval;
static catalog_profile_counts_t catalog_profile_last_sample;
static guint64 catalog_profile_sample_time;
static guint64 catalog_profile_sample_seconds;
static catalog_profile_peer_t catalog_profile_peers[CATALOG_PROFILE_MAX_PEERS];
static guint catalog_profile_peer_count;
static gint64 catalog_profile_last_dump;

#define CP_INC(field) do { \
           catalog_profile_total.field++; catalog_profile_interval.field++; \
} while (0)
#define CP_ADD(field, amount) do { \
           guint64 _amount = (guint64)(amount); \
           catalog_profile_total.field += _amount; \
           catalog_profile_interval.field += _amount; \
} while (0)
#define CP_MAX(field, amount) do { \
           guint64 _amount = (guint64)(amount); \
           if (_amount > catalog_profile_total.field) catalog_profile_total.field = _amount; \
           if (_amount > catalog_profile_interval.field) catalog_profile_interval.field = _amount; \
} \
        while (0)

static void catalog_profile_duration(guint64 us, const char *phase) {
   if (!strcmp(phase, "build")) {
      CP_ADD(build_us, us);
      CP_MAX(build_max_us, us);
   } else if (!strcmp(phase, "drain")) {
      CP_ADD(drain_us, us);
      CP_MAX(drain_max_us, us);
   } else if (!strcmp(phase, "ack")) {
      CP_ADD(ack_us, us);
      CP_MAX(ack_max_us, us);
   } else if (!strcmp(phase, "apply")) {
      CP_ADD(apply_us, us);
      CP_MAX(apply_max_us, us);
   } else if (!strcmp(phase, "refresh")) {
      CP_ADD(refresh_us, us);
      CP_MAX(refresh_max_us, us);
   }
}

static catalog_profile_peer_t *catalog_profile_peer(const char *node) {
   if (!node || !*node || strlen(node) >= sizeof(catalog_profile_peers[0].node)) {
      return NULL;
   }

   for (guint i = 0 ; i < catalog_profile_peer_count ; i++) {
      if (!strcmp(catalog_profile_peers[i].node, node)) {
         return &catalog_profile_peers[i];
      }
   }

   if (catalog_profile_peer_count == CATALOG_PROFILE_MAX_PEERS) {
      return NULL;
   }
   catalog_profile_peer_t *peer = &catalog_profile_peers[catalog_profile_peer_count++];
   g_strlcpy(peer->node, node, sizeof(peer->node));
   rrserver_links_format_node(node, peer->label, sizeof(peer->label));

   return peer;
}

/* Fixed classifications allow us to spot pathological service descriptors without allocating or storing untrusted inventory labels. */
enum catalog_profile_kind {
   CP_KIND_NODE, CP_KIND_RIG, CP_KIND_VFO, CP_KIND_ROOM,
   CP_KIND_SERVICE, CP_KIND_MENU, CP_KIND_MENU_ITEM, CP_KIND_OTHER, CP_KIND_COUNT
};
static const char *catalog_profile_kind_names[] = {
   "node", "rig", "vfo", "room", "service", "rig-menu", "rig-menu-item", "other"
};

typedef struct {
   guint64 count, accepted, rejected, bytes, apply_us, max_apply_us;
} catalog_kind_counts_t;
static catalog_kind_counts_t catalog_kind_total[CP_KIND_COUNT], catalog_kind_interval[CP_KIND_COUNT];
static const char *catalog_reject_names[] = {
   "owner", "parent", "service", "identity", "room-authority", "memory", "capacity"
};

static guint64 catalog_reject_total[RR_CAT_REJECT_COUNT], catalog_reject_interval[RR_CAT_REJECT_COUNT];


static unsigned catalog_profile_classify(dict *d) {
   if (!d) {
      return CP_KIND_OTHER;
   }

   if (dict_get(d, "catalog.service", NULL)) {
      const char *kind = dict_get(d, "inventory.kind", "");

      if (!strcmp(kind, "rig-menu-item")) {
         return CP_KIND_MENU_ITEM;
      }

      if (!strcmp(kind, "rig-menu")) {
         return CP_KIND_MENU;
      }

      return CP_KIND_SERVICE;
   }
   const char *type = dict_get(d, "object.type", "");

   if (!strcmp(type, "node")) {
      return CP_KIND_NODE;
   }

   if (!strcmp(type, "rig")) {
      return CP_KIND_RIG;
   }

   if (!strcmp(type, "vfo")) {
      return CP_KIND_VFO;
   }

   if (dict_get(d, "catalog.room", NULL)) {
      return CP_KIND_ROOM;
   }

   return CP_KIND_OTHER;
}

void rr_relay_catalog_profile_receive(dict *descriptor, bool accepted, enum rr_catalog_reject reason, long long apply_us, size_t payload_bytes) {
   CP_INC(received);
   CP_ADD(received_bytes, payload_bytes);

   if (accepted) {
      CP_INC(accepted);
   } else {
      CP_INC(rejected);

      if (reason <= RR_CAT_OK || reason >= RR_CAT_REJECT_COUNT) {
         reason = RR_CAT_IDENTITY;
      }
      catalog_reject_total[reason]++;
      catalog_reject_interval[reason]++;
   }
   const guint64 elapsed = apply_us > 0 ? (guint64) apply_us : 0;
   catalog_profile_duration(elapsed, "apply");
   const unsigned kind = catalog_profile_classify(descriptor);
   catalog_kind_counts_t *t = &catalog_kind_total[kind], *w = &catalog_kind_interval[kind];
   t->count++;
   w->count++;
   t->bytes += payload_bytes;
   w->bytes += payload_bytes;

   if (accepted) {
      t->accepted++;
      w->accepted++;
   } else {
      t->rejected++;
      w->rejected++;
   }
   t->apply_us += elapsed;
   w->apply_us += elapsed;

   if (elapsed > t->max_apply_us) {
      t->max_apply_us = elapsed;
   }

   if (elapsed > w->max_apply_us) {
      w->max_apply_us = elapsed;
   }
}

static int catalog_profile_peer_compare(const void *left, const void *right) {
   const catalog_profile_peer_t *a = left, *b = right;
   const guint64 a_errors = a->ack_partial + a->timeouts + a->aborted;
   const guint64 b_errors = b->ack_partial + b->timeouts + b->aborted;

   if (a_errors != b_errors) {
      return a_errors > b_errors ? -1 : 1;
   }

   if (a->max_ack_us != b->max_ack_us) {
      return a->max_ack_us > b->max_ack_us ? -1 : 1;
   }

   if (a->sent != b->sent) {
      return a->sent > b->sent ? -1 : 1;
   }

   return strcmp(a->node, b->node);
}


#else
#define CP_INC(field) ((void)0)
#define CP_ADD(field, amount) ((void)0)
#define CP_MAX(field, amount) ((void)0)
#endif


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

static bool catalog_reject(enum rr_catalog_reject *reason, enum rr_catalog_reject why) {
   if (reason) {
      *reason = why;
   }

   return false;
}

/* Validate room authority and retain an owned descriptor snapshot. */
bool rr_relay_catalog_put_reason(const char *owner, dict *d, enum rr_catalog_reject *reason) {
   if (reason) {
      *reason = RR_CAT_OK;
   }
   /* Strict by default: unsigned catalog entries are never trusted or published.
    * Relaxed mode is only an explicit legacy compatibility concession. */
   if (!rr_relay_local_owner(owner) && !dict_get(d, "identity.signature", NULL)) {
      Log(LOG_WARN, "mesh.identity",
         "Unsigned catalog descriptor from owner %s (type=%s uuid=%s service=%s room=%s): %s",
         owner, dict_get(d, "object.type", ""), dict_get(d, "object.uuid", ""),
         dict_get(d, "catalog.service", ""), dict_get(d, "catalog.room", ""),
         cfg_get_bool("cluster.force-signed-catalog", true) ? "rejected" : "accepted by explicit configuration");
      if (cfg_get_bool("cluster.force-signed-catalog", true))
         return catalog_reject(reason, RR_CAT_IDENTITY);
   }
   if (rr_relay_local_owner(owner) && !rr_identity_public_base64()) {
      Log(LOG_WARN, "mesh.identity", "Cannot publish local unsigned catalog descriptor: server signing identity unavailable");
      return catalog_reject(reason, RR_CAT_IDENTITY);
   }
   const char *uuid = dict_get(d, "object.uuid", NULL), *room = dict_get(d, "catalog.room", NULL);
   char key[160];

   if (uuid && !rr_relay_local_owner(owner)) {
      const char *type = dict_get(d, "object.type", ""), *parent = dict_get(d, "object.owner", "");

      if (!strcmp(type, "node")) {
         if (strcmp(uuid, owner)) {
            return catalog_reject(reason, RR_CAT_OWNER);
         }
      } else if (!strcmp(type, "rig")) {
         if (strcmp(parent, owner)) {
            return catalog_reject(reason, RR_CAT_OWNER);
         }
      } else if (!strcmp(type, "vfo")) {
         struct catalog *rig = rr_relay_find_catalog(parent);

         if (!rig || strcmp(rig->owner, owner) || strcmp(dict_get(rig->descriptor, "object.type", ""), "rig")) {
            return catalog_reject(reason, RR_CAT_PARENT);
         }
      } else {
         return catalog_reject(reason, RR_CAT_IDENTITY);
      }
   }

   const char *service = dict_get(d, "catalog.service", NULL);

   if (service) {
      const char *parent = dict_get(d, "catalog.parent", "");
      struct catalog *authority = rr_relay_find_catalog(parent);
      const char *kind = dict_get(d, "inventory.kind", "");
      bool menu_child = !strcmp(kind, "rig-menu-item") && authority && !strcmp(dict_get(authority->descriptor, "inventory.kind", ""), "rig-menu");

      /* Missing/foreign parents are a distinct, high-value mesh diagnostic. Count them in the five-minute report, never log each descriptor. */
      if (!authority || strcmp(authority->owner, owner)) {
         return catalog_reject(reason, RR_CAT_PARENT);
      }

      if (uuid || strcmp(dict_get(d, "object.type", ""), "service") || !authority || strcmp(authority->owner, owner) ||
         (strcmp(dict_get(authority->descriptor, "object.type", ""), "node") &&
         strcmp(dict_get(authority->descriptor, "object.type", ""), "rig") && strcmp(dict_get(authority->descriptor, "object.type", ""), "vfo")
         && !menu_child) || (strcmp(kind, "gps") && strcmp(kind, "serial") && strcmp(kind, "media") && strcmp(kind, "cat") && strcmp(kind, "share")
         && strcmp(kind, "control") && strcmp(kind, "rig-control") && strcmp(kind, "rig-menu") && strcmp(kind, "rig-menu-item")
         && strcmp(kind, "performance-stat"))
         || strlen(service) < 9 + strlen(owner) || strlen(service) >= sizeof(key) || strncmp(service, "service:", 8)
         || strncmp(service + 8, owner, strlen(owner)) || service[8 + strlen(owner)] != ':') {
         return catalog_reject(reason, RR_CAT_SERVICE);
      }
      snprintf(key, sizeof(key), "%s", service);
   } else if (uuid && rr_object_uuid_valid(uuid)) {
      snprintf(key, sizeof(key), "%s", uuid);
   } else if (room && ws_room_name_valid(room)) {
      snprintf(key, sizeof(key), "room:%s", room);
   } else {
      return catalog_reject(reason, RR_CAT_IDENTITY);
   }
   struct catalog *c = rr_relay_find_catalog(key);
   bool replace = c && strcmp(c->owner, owner);

   if (replace && (uuid || !room || rr_relay_local_owner(owner) || !owns_station_room(owner, room) ||
      owns_station_room(c->owner, room) || !strcasecmp(room, ws_site_room()) || rrserver_rig_room_configured(room))) {
      return catalog_reject(reason, RR_CAT_ROOM_AUTH);   // Only proven station/rig authority may displace a stale room claim.
   }
   dict *copy = dict_clone(d);

   if (copy && rr_relay_local_owner(owner) && rr_identity_public_base64() && !catalog_collecting_local) {
      /* Include the authoritative owner and domain IN the signed material;
       * the envelope's untrusted routing origin is not sufficient. */
      dict_add(copy, "identity.owner", owner);
      dict_add(copy, "identity.domain", cfg_get("domain"));
      dict_add(copy, "identity.public-key", rr_identity_public_base64());
      dict_add(copy, "identity.scheme", "ed25519-catalog-v1");
      dict_add(copy, "identity.operation", "catalog-upsert");
#ifdef USE_SQLITE
      if (!rr_identity_local_revision(masterdb, cfg_get("domain"), owner, key, copy)) {
         dict_free(copy);
         return catalog_reject(reason, RR_CAT_IDENTITY);
      }
#endif
      if (!rr_identity_sign_catalog(copy)) {
         dict_free(copy);
         return catalog_reject(reason, RR_CAT_IDENTITY);
      }
   } else if (copy && dict_get(copy, "identity.signature", NULL)) {
      /* Verify signatures, owner binding and local-domain scope before
       * storing signed foreign entries. Catalog identity *trust* still
       * requires separately pinned server keys. */
      const char *signed_owner = dict_get(copy, "identity.owner", "");
      const char *signed_domain = dict_get(copy, "identity.domain", "");
      const char *scheme = dict_get(copy, "identity.scheme", "");
      const char *operation = dict_get(copy, "identity.operation", "");
      const char *problem = NULL;
      if (strcmp(signed_owner, owner)) problem = "signed owner does not match envelope origin";
      else if (g_ascii_strcasecmp(signed_domain, cfg_get("domain"))) problem = "domain mismatch";
      else if (strcmp(scheme, "ed25519-catalog-v1")) problem = "unsupported signature scheme";
      else if (strcmp(operation, "catalog-upsert")) problem = "wrong signed operation";
      else if (!rr_identity_verify_catalog(copy)) problem = "Ed25519 signature verification failed";
      if (problem) {
         char owner_label[192];
         Log(LOG_WARN, "mesh.identity",
            "Rejecting signed catalog entry from %s: %s (type=%s uuid=%s service=%s)",
            rrserver_links_format_node(owner, owner_label, sizeof(owner_label)), problem, dict_get(copy, "object.type", ""),
            dict_get(copy, "object.uuid", ""), dict_get(copy, "catalog.service", ""));
         dict_free(copy);
         return catalog_reject(reason, RR_CAT_IDENTITY);
      }
   }

   if (!copy) {
      return catalog_reject(reason, RR_CAT_MEMORY);
   }
#ifdef USE_SQLITE
   /* Trust on first verified catalog observation, persistent across restarts.
    * Never permit a subsequent catalog to silently replace a pinned key.
    * Bootstrap peers must be independently trusted for initial enrollment. */
   if (dict_get(copy, "identity.signature", NULL) &&
       !rr_identity_check_pin(masterdb, dict_get(copy, "identity.domain", ""),
         dict_get(copy, "identity.owner", ""), dict_get(copy, "identity.public-key", ""))) {
      char owner_label[192];
      Log(LOG_WARN, "mesh.identity", "Rejecting catalog entry from %s: untrusted identity key change or pin storage failure",
         rrserver_links_format_node(owner, owner_label, sizeof(owner_label)));
      dict_free(copy);
      return catalog_reject(reason, RR_CAT_IDENTITY);
   }
#endif
#ifdef USE_SQLITE
   if (!rr_relay_local_owner(owner) && dict_get(copy, "identity.signature", NULL) &&
       !dict_get(copy, "identity.revision", NULL)) {
      char owner_label[192];
      Log(LOG_WARN, "mesh.identity",
          "Rejected signed catalog entry %s from %s: peer did not supply identity.revision; upgrade all servers to the revision-aware protocol",
          key, rrserver_links_format_node(owner, owner_label, sizeof(owner_label)));
      dict_free(copy);
      return catalog_reject(reason, RR_CAT_IDENTITY);
   }
   if (!rr_relay_local_owner(owner) && dict_get(copy, "identity.signature", NULL) &&
       !rr_identity_accept_revision(masterdb, cfg_get("domain"), owner, key, copy)) {
      char owner_label[192];
      Log(LOG_WARN, "mesh.identity", "Rejected stale or conflicting signed catalog revision for %s owned by %s (revision=%s)",
         key, rrserver_links_format_node(owner, owner_label, sizeof(owner_label)), dict_get(copy, "identity.revision", ""));
      dict_free(copy);
      return catalog_reject(reason, RR_CAT_IDENTITY);
   }
#endif

   if (replace && rr_relay_local_owner(c->owner)) {
#ifdef USE_SQLITE
      bool exists, deleted;

      if (!db_room_status(masterdb, room, &exists, &deleted) || (exists && !db_room_delete(masterdb, room, "server-link", true, false))) {
         Log(LOG_WARN, "link", "Unable to remove stale local room metadata for %s", room);
         dict_free(copy);

         return catalog_reject(reason, RR_CAT_ROOM_AUTH);
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
      char owner_label[192];
      Log(LOG_INFO, "link", "Removed stale local room %s; authoritative owner is %s (chat history retained)", room, rrserver_links_format_node(owner,
         owner_label, sizeof(owner_label)));
   }

   if (!c) {
      if (rr_relay_catalog_count == MAX_CATALOG) {
         dict_free(copy);

         return catalog_reject(reason, RR_CAT_CAPACITY);
      }
      c = calloc(1, sizeof(*c));

      if (!c) {
         dict_free(copy);

         return catalog_reject(reason, RR_CAT_MEMORY);
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

   return true;
}

/* Build and store one local object descriptor. */
bool rr_relay_catalog_put(const char *owner, dict *d) {
   return rr_relay_catalog_put_reason(owner, d, NULL);
}

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

static void collect_service(dict *row, void *context);

/* Process-local performance inventory, replicated as ordinary node-owned read-only discovery services. These are cached observations, not a remote
 * process-control API. Sampling follows the normal five-minute profile dump. */
static void collect_performance_service(const char *name, const char *description, const char *state) {
   dict *row = rr_inventory_row("", 0, "performance-stat", name, NULL);

   if (!row) {
      return;
   }
   dict_add(row, "inventory.description", description ? description : "Unavailable");
   dict_add(row, "inventory.state", state ? state : "observed");
   dict_add(row, "inventory.access", "read-only");
   collect_service(row, (void *)rrserver_links_node());
}

static void collect_process_metrics(void) {
#ifdef __linux__
   FILE *status = fopen("/proc/self/status", "r");

   if (status) {
      char line[256], rss[80] = "", peak[80] = "", threads[80] = "";
      while (fgets(line, sizeof(line), status)) {
         if (!strncmp(line, "VmRSS:", 6)) {
            g_strlcpy(rss, line + 6, sizeof(rss));
         } else if (!strncmp(line, "VmHWM:", 6)) {
            g_strlcpy(peak, line + 6, sizeof(peak));
         } else if (!strncmp(line, "Threads:", 8)) {
            g_strlcpy(threads, line + 8, sizeof(threads));
         }
      }
      fclose(status);
      g_strstrip(rss);
      g_strstrip(peak);
      g_strstrip(threads);

      if (*rss) {
         char description[256];
         snprintf(description, sizeof(description), "Resident physical memory (VmRSS): %s; peak resident (VmHWM): %s", rss, *peak ? peak : "unknown");
         collect_performance_service("Process memory", description, rss);
      }

      if (*threads) {
         collect_performance_service("Threads", "Active OS threads in this server process", threads);
      }
   }
   /* Only the server process's open file descriptors are counted. PTY master endpoints are classified conservatively; no device paths are disclosed through the
    * inventory. */
   GDir *fd_dir = g_dir_open("/proc/self/fd", 0, NULL);

   if (fd_dir) {
      guint count = 0, ptys = 0;
      const char *entry;
      while ((entry = g_dir_read_name(fd_dir))) {
         if (!g_ascii_isdigit(entry[0])) {
            continue;
         }
         char path[128], target[256];
         snprintf(path, sizeof(path), "/proc/self/fd/%s", entry);
         ssize_t n = readlink(path, target, sizeof(target) - 1);

         if (n < 0) {
            continue;
         }
         target[n] = 0;
         count++;

         if (!strcmp(target, "/dev/ptmx") || g_str_has_prefix(target, "/dev/pts/")) {
            ptys++;
         }
      }
      g_dir_close(fd_dir);
      char summary[128], detail[160];
      snprintf(summary, sizeof(summary), "%u open", count);
      snprintf(detail, sizeof(detail), "%u file descriptors open in rrserver (includes sockets, files and event descriptors)", count);
      collect_performance_service("Open file descriptors", detail, summary);
      snprintf(summary, sizeof(summary), "%u open", ptys);
      snprintf(detail, sizeof(detail), "%u open PTY-associated descriptors (masters/slaves, not necessarily distinct terminals)", ptys);
      collect_performance_service("PTY descriptors", detail, summary);
   }
#endif
}

static void collect_event_performance(void) {
   event_profile_summary_t stats;

   if (!event_profile_summary(&stats)) {
      collect_performance_service("Event dispatch", "Event callback timing requires features.profiling=true at build time", "disabled");

      return;
   }
   char status[128], details[400];
   snprintf(status, sizeof(status), "%llu events", (unsigned long long)stats.emits);
   snprintf(details, sizeof(details),
      "Cumulative event emissions: %llu; payload bytes: %llu; inline callbacks: %llu; average %.1f us; maximum %llu us; slowest event: %s", (unsigned long long)
      stats.emits, (unsigned long long)stats.payload_bytes, (unsigned long long)stats.inline_calls, stats.inline_calls ? (double)stats.inline_us / stats.
      inline_calls : 0.0, (unsigned long long)stats.max_inline_us, *stats.slowest_event ? stats.slowest_event : "not observed");
   collect_performance_service("Event callback timing", details, status);
   snprintf(status, sizeof(status), "%llu queued", (unsigned long long)stats.queued);
   snprintf(details, sizeof(details),
      "Cumulative deferred callbacks: queued %llu, completed %llu; handler average %.1f us, maximum %llu us; average dispatch queue wait %.1f us", (unsigned
         long long)stats.queued, (unsigned long long)stats.completed, stats.completed ? (double)stats.deferred_us / stats.completed : 0.0, (unsigned long long)
      stats.max_deferred_us, stats.completed ? (double)stats.queue_wait_us / stats.completed : 0.0);
   collect_performance_service("Deferred event dispatch", details, status);
}

static void collect_catalog_performance(void) {
   rr_catalog_profile_info_t stats;

   if (!rr_relay_catalog_profile_summary(&stats)) {
      collect_performance_service("Catalog profiling", "Enable features.profiling and rebuild to collect five-minute performance samples", "disabled");

      return;
   }
   char details[360], status[120];
   snprintf(status, sizeof(status), "%llu s sample", (unsigned long long)stats.sample_seconds);
   snprintf(details, sizeof(details),
      "Sample Unix time: %llu; catalog entries: %zu; outgoing snapshots: %llu; queue completed: %llu; pending snapshots: %llu; awaiting acknowledgments: %llu",
      (unsigned long long)stats.sample_time, stats.catalog_entries, (unsigned long long)stats.snapshots, (unsigned long long)stats.queue_complete, (unsigned
         long long)stats.pending_snapshots, (unsigned long long)stats.pending_acks);
   collect_performance_service("Catalog synchronization", details, status);
   snprintf(status, sizeof(status), "%llu complete / %llu partial", (unsigned long long)stats.ack_complete, (unsigned long long)stats.ack_partial);
   snprintf(details, sizeof(details),
      "Last completed sample: %llu complete acknowledgments, %llu incomplete/rejected, %llu timeouts. End-to-end ACK average %.2f ms; maximum %.2f ms", (
         unsigned long long)stats.ack_complete, (unsigned long long)stats.ack_partial, (unsigned long long)stats.ack_timeouts, stats.ack_complete + stats.
      ack_partial ? (double)stats.ack_us / (stats.ack_complete + stats.ack_partial) / 1000.0 : 0.0, stats.max_ack_us / 1000.0);
   collect_performance_service("Catalog acknowledgments", details, status);
   snprintf(status, sizeof(status), "%llu accepted / %llu rejected", (unsigned long long)stats.descriptors_accepted, (unsigned long long)stats.
      descriptors_rejected);
   snprintf(details, sizeof(details),
      "Received %llu catalog descriptors, accepted %llu, rejected %llu. Average validation %.1f us, maximum %llu us; received %llu JSON bytes", (unsigned long
         long)stats.descriptors_received, (unsigned long long)stats.descriptors_accepted, (unsigned long long)stats.descriptors_rejected, stats.
      descriptors_received ? (double)stats.apply_us / stats.descriptors_received : 0.0, (unsigned long long)stats.max_apply_us, (unsigned long long)stats.
      payload_bytes);
   collect_performance_service("Catalog descriptor processing", details, status);
   snprintf(status, sizeof(status), "%.2f ms avg", stats.snapshots ? (double)stats.snapshot_build_us / stats.snapshots / 1000.0 : 0.0);
   snprintf(details, sizeof(details), "Snapshot build time: average %s, maximum %.2f ms", status, stats.max_build_us / 1000.0);
   collect_performance_service("Catalog snapshot preparation", details, status);
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
   /* Parent may itself be a service key (nested MENU); hash it to keep catalog keys within the fixed-size wire/key limits. */
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

/* Mirror rig property descriptors and CAT menu metadata across relay hops. These are inventory-only rows; CAT operations remain owner-local. */
typedef struct relay_rig_menu_ctx {
   const char *rig_id;
   const char *model;
   char menu_id[160];
} relay_rig_menu_ctx_t;

static bool collect_rig_property(const rr_property_descriptor_t *d, void *user) {
   /* Yaesu EX menu properties already appear under MENU with their descriptive labels. Do not duplicate them in the Controls folder. */
   if (d->name && !strncmp(d->name, "menu.", 5)) {
      return false;
   }
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

   if (!id || strlen(id) != 4) {
      return;
   }
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

   if (!backend || !backend->type->api->menu_foreach) {
      return;
   }
   const char *model = backend->type->api->model_name ? backend->type->api->model_name(backend) : NULL;

   if (!model || !strcmp(model, "Unknown Yaesu")) {
      return;
   }
   relay_rig_menu_ctx_t ctx = {
      .rig_id = id, .model = model
   };
   snprintf(ctx.menu_id, sizeof(ctx.menu_id), "%s.menu", id);
   dict *menu = rr_inventory_row("", 2, "rig-menu", "MENU", NULL);

   if (menu) {
      dict_add(menu, "inventory.description", model);
      collect_service(menu, (void *)id);
   }
   char *mh = g_compute_checksum_for_string(G_CHECKSUM_SHA256, "MENU", -1);
   char *ph = g_compute_checksum_for_string(G_CHECKSUM_SHA256, id, -1);
   snprintf(ctx.menu_id, sizeof(ctx.menu_id), "service:%s:%.24s:%.8s:%.32s", rrserver_links_node(), ph ? ph : "", "rig-menu", mh ? mh : "");
   g_free(mh);
   g_free(ph);
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
   rr_backend_t *backend = rr_server_rig_backend(radio);

   if (c && backend && backend->type && backend->type->api && backend->type->api->model_name) {
      const char *model = backend->type->api->model_name(backend);

      if (model && *model && strcmp(model, "Unknown Yaesu")) {
         dict_add(c->descriptor, "object.model", model);
      }
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
   catalog_collecting_local = true;
   add_object(rrserver_links_node(), "node", NULL, cfg_get("station.name"), ws_site_room());
   collect_services(rrserver_links_node(), "station");
   collect_process_metrics();
   collect_event_performance();
   collect_catalog_performance();
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
   catalog_collecting_local = false;
   /* Finalization barrier: no local descriptor leaves this collection pass
    * until every collector has added its fields. Re-seal each completed
    * descriptor; a failure leaves it unsigned and publication is blocked. */
   for (struct catalog *c = rr_relay_catalogs; c; c = c->next) {
      if (!rr_relay_local_owner(c->owner) || !c->seen) continue;
      dict_del(c->descriptor, "identity.signature");
      dict_add(c->descriptor, "identity.owner", c->owner);
      dict_add(c->descriptor, "identity.domain", cfg_get("domain"));
      dict_add(c->descriptor, "identity.public-key", rr_identity_public_base64());
      dict_add(c->descriptor, "identity.scheme", "ed25519-catalog-v1");
      dict_add(c->descriptor, "identity.operation", "catalog-upsert");
#ifdef USE_SQLITE
      if (!rr_identity_local_revision(masterdb, cfg_get("domain"), c->owner, c->key, c->descriptor)) {
         Log(LOG_CRIT, "mesh.identity", "Cannot persist catalog revision for %s; publication blocked", c->key);
         continue;
      }
#endif
      if (!rr_identity_sign_catalog(c->descriptor))
         Log(LOG_CRIT, "mesh.identity", "Unable to finalize local catalog signature for %s; publication blocked", c->key);
   }
}

/* Pace initial catalog snapshots.  The link's output queue is only 256 KiB;
 * dumping every Yaesu MENU item synchronously disconnects the peer. */
typedef struct relay_catalog_snapshot {
   char *dest;
   GPtrArray *messages;
   guint offset;
   gint64 next_send;
   gint64 started_at;
   char *sync_id;
} relay_catalog_snapshot_t;
static GHashTable *relay_catalog_snapshots;
static GHashTable *relay_catalog_awaiting_ack;
typedef struct {
   char *dest;
   gint64 started;
   guint count;
} catalog_ack_wait_t;
static void ack_wait_free(gpointer ptr) {
   catalog_ack_wait_t *w = ptr;
   g_free(w->dest);
   g_free(w);
}

#ifdef USE_PROFILING
void rr_relay_catalog_profile_dump(void) {
   const gint64 now = g_get_monotonic_time();

   if (catalog_profile_last_dump && now - catalog_profile_last_dump < 300 * G_USEC_PER_SEC) {
      return;
   }
   double sample_s = catalog_profile_last_dump ? (now - catalog_profile_last_dump) / 1000000.0 : 300.0;
   catalog_profile_last_dump = now;

   /* Capture and reset the interval before logging. Logging callbacks can cause state changes and must not lose their counts. */
   const catalog_profile_counts_t t = catalog_profile_total;
   const catalog_profile_counts_t w = catalog_profile_interval;
   memset(&catalog_profile_interval, 0, sizeof(catalog_profile_interval));
   guint64 rejection_delta[RR_CAT_REJECT_COUNT];
   memcpy(rejection_delta, catalog_reject_interval, sizeof(rejection_delta));
   memset(catalog_reject_interval, 0, sizeof(catalog_reject_interval));
   catalog_profile_last_sample = w;
   catalog_profile_sample_seconds = (guint64) (sample_s + 0.5);
   catalog_profile_sample_time = (guint64) time(NULL);

   guint queued_syncs = 0, pending_acks = 0;
   guint64 queued_descriptors = 0;

   if (relay_catalog_snapshots) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, relay_catalog_snapshots);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         relay_catalog_snapshot_t *s = value;
         queued_syncs++;

         if (s->offset < s->messages->len) {
            queued_descriptors += s->messages->len - s->offset;
         }
      }
   }

   if (relay_catalog_awaiting_ack) {
      pending_acks = g_hash_table_size(relay_catalog_awaiting_ack);
   }

   GString *report = g_string_new(NULL);
   g_string_append_printf(report,
      "Catalog synchronization statistics (cumulative; last sample %.0f s; entries=%zu, queued=%u, pending-acks=%u, remaining-items=%llu):", sample_s,
      rr_relay_catalog_count, queued_syncs, pending_acks, (unsigned long long)queued_descriptors);
   g_string_append_c(report, '\n');
   g_string_append_printf(report, "  snapshots: started=%llu (+%llu), queue-complete=%llu (+%llu), aborted=%llu (+%llu), superseded=%llu (+%llu)", (unsigned long
      long)t.snapshots, (unsigned long long)w.snapshots, (unsigned long long)t.queued, (unsigned long long)w.queued, (unsigned long long)t.aborted, (unsigned
         long long)w.aborted, (unsigned long long)t.superseded, (unsigned long long)w.superseded);
   g_string_append_c(report, '\n');
   g_string_append_printf(report, "  acknowledgments: accepted=%llu (+%llu), partial=%llu (+%llu), timed-out=%llu (+%llu), cancelled=%llu (+%llu)", (unsigned long
      long)t.ack_ok, (unsigned long long)w.ack_ok, (unsigned long long)t.ack_partial, (unsigned long long)w.ack_partial, (unsigned long long)t.ack_timeout, (
         unsigned long long)w.ack_timeout, (unsigned long long)t.ack_cancelled, (unsigned long long)w.ack_cancelled);
   g_string_append_c(report, '\n');
   g_string_append_printf(report,
      "  descriptors: prepared=%llu (+%llu), sent=%llu (+%llu), peer-received=%llu (+%llu), peer-accepted=%llu (+%llu), peer-rejected=%llu (+%llu)", (unsigned
         long long)t.prepared, (unsigned long long)w.prepared, (unsigned long long)t.sent, (unsigned long long)w.sent, (unsigned long long)t.ack_received, (
         unsigned long long)w.ack_received, (unsigned long long)t.ack_accepted, (unsigned long long)w.ack_accepted, (unsigned long long)t.ack_rejected, (
         unsigned long long)w.ack_rejected);
   g_string_append_c(report, '\n');
   g_string_append_printf(report, "  payload JSON bytes: prepared=%llu (+%llu), sent=%llu (+%llu), received=%llu (+%llu) (not transport overhead)", (unsigned long
      long)t.prepared_bytes, (unsigned long long)w.prepared_bytes, (unsigned long long)t.sent_bytes, (unsigned long long)w.sent_bytes, (unsigned long long)t.
      received_bytes, (unsigned long long)w.received_bytes);
   g_string_append_c(report, '\n');
   g_string_append_printf(report,
      "  inbound catalog apply: received=%llu (+%llu), accepted=%llu (+%llu), rejected=%llu (+%llu), avg=%.1f us, max=%llu us (sample avg=%.1f us, max=%llu us)",
      (unsigned long long)t.received, (unsigned long long)w.received, (unsigned long long)t.accepted, (unsigned long long)w.accepted, (unsigned long long)t.
      rejected, (unsigned long long)w.rejected, t.received ? (double)t.apply_us / t.received : 0.0, (unsigned long long)t.apply_max_us, w.received ? (double)w.
      apply_us / w.received : 0.0, (unsigned long long)w.apply_max_us);

   for (unsigned i = RR_CAT_OWNER ; i < RR_CAT_REJECT_COUNT ; ++i) {
      if (catalog_reject_total[i]) {
         g_string_append_c(report, '\n');
         g_string_append_printf(report, "    reject/%s: total=%llu (+%llu)", catalog_reject_names[i - 1], (unsigned long long)catalog_reject_total[i], (unsigned long
            long)rejection_delta[i]);
      }
   }

   for (unsigned i = 0 ; i < CP_KIND_COUNT ; i++) {
      const catalog_kind_counts_t *total = &catalog_kind_total[i];
      const catalog_kind_counts_t *sample = &catalog_kind_interval[i];

      if (!total->count) {
         continue;
      }
      g_string_append_c(report, '\n');
      g_string_append_printf(report,
         "    apply/%s: count=%llu (+%llu), accepted=%llu (+%llu), rejected=%llu (+%llu), bytes=%llu (+%llu), avg=%.1f us, max=%llu us (sample avg=%.1f us, max=%llu us)",
         catalog_profile_kind_names[i], (unsigned long long)total->count, (unsigned long long)sample->count, (unsigned long long)total->accepted, (unsigned long
            long)sample->accepted, (unsigned long long)total->rejected, (unsigned long long)sample->rejected, (unsigned long long)total->bytes, (unsigned long
            long)sample->bytes, (double)total->apply_us / total->count, (unsigned long long)total->max_apply_us, sample->count ? (double)sample->apply_us /
         sample->count : 0.0, (unsigned long long)sample->max_apply_us);
   }

   memset(catalog_kind_interval, 0, sizeof(catalog_kind_interval));
   g_string_append_c(report, '\n');
   g_string_append_printf(report,
      "  snapshot build: avg=%.2f ms, max=%.2f ms (sample avg=%.2f ms, max=%.2f ms); queue drain: avg=%.2f ms, max=%.2f ms (sample avg=%.2f ms, max=%.2f ms)", t
      .snapshots ? (double)t.build_us / t.snapshots / 1000.0 : 0.0, t.build_max_us / 1000.0, w.snapshots ? (double)w.build_us / w.snapshots / 1000.0 : 0.0, w.
      build_max_us / 1000.0, t.queued ? (double)t.drain_us / t.queued / 1000.0 : 0.0, t.drain_max_us / 1000.0, w.queued ? (double)w.drain_us / w.queued / 1000.0
      : 0.0, w.drain_max_us / 1000.0);
   g_string_append_c(report, '\n');
   g_string_append_printf(report,
      "  end-to-end ACK latency: avg=%.2f ms, max=%.2f ms (sample avg=%.2f ms, max=%.2f ms); refreshes=%llu (+%llu), delta send attempts=%llu (+%llu), remove attempts=%llu (+%llu), refresh avg=%.2f ms, max=%.2f ms",
      t.ack_ok + t.ack_partial ? (double)t.ack_us / (t.ack_ok + t.ack_partial) / 1000.0 : 0.0, t.ack_max_us / 1000.0, w.ack_ok + w.ack_partial ? (double)w.
      ack_us / (w.ack_ok + w.ack_partial) / 1000.0 : 0.0, w.ack_max_us / 1000.0, (unsigned long long)t.refreshes, (unsigned long long)w.refreshes, (unsigned
         long long)t.delta_updates, (unsigned long long)w.delta_updates, (unsigned long long)t.delta_removes, (unsigned long long)w.delta_removes, t.refreshes ?
      (double)t.refresh_us / t.refreshes / 1000.0 : 0.0, t.refresh_max_us / 1000.0);

   /* Bound the log even if a future mesh has hundreds of neighbors. */
   catalog_profile_peer_t peers[CATALOG_PROFILE_MAX_PEERS];
   memcpy(peers, catalog_profile_peers, catalog_profile_peer_count * sizeof(peers[0]));
   qsort(peers, catalog_profile_peer_count, sizeof(peers[0]), catalog_profile_peer_compare);
   const guint limit = MIN(catalog_profile_peer_count, 12u);

   for (guint i = 0 ; i < limit ; i++) {
      const catalog_profile_peer_t *p = &peers[i];
      char label[192];
      g_string_append_c(report, '\n');
      g_string_append_printf(report,
         "  neighbor %s: snapshots=%llu queued=%llu acked=%llu partial=%llu timeout=%llu aborted=%llu, sent=%llu accepted=%llu rejected=%llu, last-ack=%.2f ms max-ack=%.2f ms",
         /* Retain known names after a neighbor disconnects. */
         strcmp(rrserver_links_format_node(p->node, label, sizeof(label)), p->node) ?
         label : (p->label[0] ? p->label : p->node), (unsigned long long)p->snapshots, (unsigned long long)p->queued, (unsigned long long)p->ack_ok, (unsigned
            long long)p->ack_partial, (unsigned long long)p->timeouts, (unsigned long long)p->aborted, (unsigned long long)p->sent, (unsigned long long)p->
         accepted, (unsigned long long)p->rejected, p->last_ack_us / 1000.0, p->max_ack_us / 1000.0);
   }

   if (catalog_profile_peer_count > limit) {
      g_string_append_c(report, '\n');
      g_string_append_printf(report, "  ... %u additional neighbor profiles omitted (top %u prioritize failures, latency, then volume)", catalog_profile_peer_count -
         limit, limit);
   }
   Log(LOG_INFO, "profile", "%s", report->str);
   g_string_free(report, TRUE);
   /* Update published read-only performance inventory on the next ordinary catalog refresh. Never perform an immediate recursive refresh here. */
   rr_relay_catalog_dirty = true;
}
#endif

bool rr_relay_catalog_profile_summary(rr_catalog_profile_info_t *info) {
   if (!info) {
      return false;
   }
   memset(info, 0, sizeof(*info));
   info->catalog_entries = rr_relay_catalog_count;
   info->pending_snapshots = relay_catalog_snapshots ? g_hash_table_size(relay_catalog_snapshots) : 0;
   info->pending_acks = relay_catalog_awaiting_ack ? g_hash_table_size(relay_catalog_awaiting_ack) : 0;
#ifdef USE_PROFILING
   /* Report the last complete sample, not the partly collected next period. */
   const catalog_profile_counts_t *w = &catalog_profile_last_sample;
   info->sample_time = catalog_profile_sample_time;
   info->sample_seconds = catalog_profile_sample_seconds;
   info->snapshots = w->snapshots;
   info->queue_complete = w->queued;
   info->ack_complete = w->ack_ok;
   info->ack_partial = w->ack_partial;
   info->ack_timeouts = w->ack_timeout;
   info->descriptors_received = w->received;
   info->descriptors_accepted = w->accepted;
   info->descriptors_rejected = w->rejected;
   info->snapshot_build_us = w->build_us;
   info->max_build_us = w->build_max_us;
   info->ack_us = w->ack_us;
   info->max_ack_us = w->ack_max_us;
   info->apply_us = w->apply_us;
   info->max_apply_us = w->apply_max_us;
   info->payload_bytes = w->received_bytes;

   return true;
#else

   return false;
#endif
}

void rr_relay_catalog_ack(const char *origin, const char *sync_id, guint received, guint accepted, guint rejected) {
   char origin_label[192];
   rrserver_links_format_node(origin, origin_label, sizeof(origin_label));

   if (!relay_catalog_awaiting_ack || !sync_id || !*sync_id) {
      return;
   }
   catalog_ack_wait_t *w = g_hash_table_lookup(relay_catalog_awaiting_ack, sync_id);

   if (!w || strcmp(w->dest, origin)) {
      return;
   }
   const gint64 elapsed_us = g_get_monotonic_time() - w->started;
   const bool complete = received == w->count && accepted == received && rejected == 0;
#ifdef USE_PROFILING

   if (complete) {
      CP_INC(ack_ok);
   } else {
      CP_INC(ack_partial);
   }
   CP_ADD(ack_received, received);
   CP_ADD(ack_accepted, accepted);
   CP_ADD(ack_rejected, rejected);
   catalog_profile_duration(elapsed_us > 0 ? (guint64) elapsed_us : 0, "ack");
   catalog_profile_peer_t *peer = catalog_profile_peer(origin);

   if (peer) {
      if (complete) {
         peer->ack_ok++;
      } else {
         peer->ack_partial++;
      }
      peer->accepted += accepted;
      peer->rejected += rejected;
      peer->last_ack_us = elapsed_us > 0 ? (guint64) elapsed_us : 0;

      if (peer->last_ack_us > peer->max_ack_us) {
         peer->max_ack_us = peer->last_ack_us;
      }
   }
#endif
   rr_relay_sync_status(origin, false, complete);
   Log(complete ? LOG_INFO : LOG_WARN, "relay.catalog",
      "Catalog sent to %s: %s; accepted=%u rejected=%u missing=%u of %u entries, %.3f s (sync %s)%s",
      origin_label, complete ? "complete" : "NOT fully applied", accepted, rejected,
      w->count > received ? w->count - received : 0, w->count, elapsed_us / 1000000.0,
      sync_id, complete ? "" : "; see the receiving server's rejection summary");
   g_hash_table_remove(relay_catalog_awaiting_ack, sync_id);
}


static void snapshot_free(gpointer ptr) {
   relay_catalog_snapshot_t *s = ptr;

   if (!s) {
      return;
   }
   g_ptr_array_free(s->messages, TRUE);
   g_free(s->dest);
   g_free(s->sync_id);
   g_free(s);
}

/* Return FALSE when the snapshot is complete or the neighbor is lost. The pump owns removal: never mutate its hash table during iteration. */
static gboolean snapshot_tick(relay_catalog_snapshot_t *s) {
   char dest_label[192];
   rrserver_links_format_node(s->dest, dest_label, sizeof(dest_label));
   const char *route = rrserver_links_route(s->dest);

   if (!route || strcmp(route, s->dest)) {
#ifdef USE_PROFILING
      CP_INC(aborted);
      catalog_profile_peer_t *peer = catalog_profile_peer(s->dest);

      if (peer) {
         peer->aborted++;
      }
#endif
      Log(LOG_WARN, "relay.catalog", "Aborted catalog delivery to %s after %u/%u descriptors (%.3f s): neighbor unreachable", dest_label, s->offset, s->messages
         ->len, (g_get_monotonic_time() - s->started_at) / 1000000.0);

      return FALSE;
   }

   for (unsigned n = 0 ; n < 8 && s->offset < s->messages->len ; n++) {
      dict *d = g_ptr_array_index(s->messages, s->offset);

      if (!rrserver_links_send(s->dest, d)) {
#ifdef USE_PROFILING
         CP_INC(aborted);
         catalog_profile_peer_t *peer = catalog_profile_peer(s->dest);

         if (peer) {
            peer->aborted++;
         }
#endif
         Log(LOG_WARN, "relay.catalog", "Catalog sync %s to %s failed sending item %u/%u (%.3f s); no acknowledgment expected", s->sync_id, dest_label, s->
            offset + 1, s->messages->len, (g_get_monotonic_time() - s->started_at) / 1000000.0);

         return FALSE;
      }
      s->offset++;
#ifdef USE_PROFILING

      /* The first queue item is catalog-begin, not a descriptor. */
      if (s->offset > 1) {
         CP_INC(sent);
         CP_ADD(sent_bytes, strlen(dict_get(d, "payload", "")));
         catalog_profile_peer_t *peer = catalog_profile_peer(s->dest);

         if (peer) {
            peer->sent++;
         }
      }
#endif
   }

   if (s->offset == s->messages->len) {
      gboolean end_sent = FALSE;
      dict *end = dict_new();

      if (end) {
         dict_add(end, "op", "link.application");
         dict_add(end, "node", rrserver_links_node());
         dict_add(end, "dest", s->dest);
         dict_add(end, "kind", "catalog-end");
         dict_add(end, "session", s->sync_id);
         dict_add(end, "username", "");
         dict_add(end, "payload", "{}");
         end_sent = rrserver_links_send(s->dest, end);

         if (!end_sent) {
            Log(LOG_WARN, "relay.catalog", "Failed to send catalog-end to %s for sync %s", dest_label, s->sync_id);
         }
         dict_free(end);
      }

      if (!end_sent) {
#ifdef USE_PROFILING
         CP_INC(aborted);
         catalog_profile_peer_t *peer = catalog_profile_peer(s->dest);

         if (peer) {
            peer->aborted++;
         }
#endif

         return FALSE;
      }
#ifdef USE_PROFILING
      CP_INC(queued);
      catalog_profile_duration((guint64) MAX(0, g_get_monotonic_time() - s->started_at), "drain");
      catalog_profile_peer_t *peer = catalog_profile_peer(s->dest);

      if (peer) {
         peer->queued++;
      }
#endif

      if (!relay_catalog_awaiting_ack) {
         relay_catalog_awaiting_ack = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, ack_wait_free);
      }
      catalog_ack_wait_t *wait = g_new0(catalog_ack_wait_t, 1);
      wait->dest = g_strdup(s->dest);
      wait->started = s->started_at;
      wait->count = s->offset - 1;
      g_hash_table_replace(relay_catalog_awaiting_ack, g_strdup(s->sync_id), wait);
      double elapsed = (g_get_monotonic_time() - s->started_at) / 1000000.0;
      Log(LOG_INFO, "relay.catalog",
         "Finished queuing %u catalog descriptors to %s in %.3f s (%.1f descriptors/s); transport delivery and remote application not yet confirmed", s->offset
         - 1, dest_label, elapsed, elapsed > 0 ? (s->offset - 1) / elapsed : 0.0);

      return FALSE;
   }

   return TRUE;
}

/* Called from the Mongoose server loop; GLib timeouts are not dispatched there. */
void rr_relay_catalog_pump(void) {
   char dest_label[192];
   gint64 now = g_get_monotonic_time();

   if (relay_catalog_awaiting_ack) {
      GHashTableIter pending;
      gpointer key, value;
      g_hash_table_iter_init(&pending, relay_catalog_awaiting_ack);
      while (g_hash_table_iter_next(&pending, &key, &value)) {
         catalog_ack_wait_t *w = value;

         if (now - w->started >= 30 * G_USEC_PER_SEC) {
#ifdef USE_PROFILING
            CP_INC(ack_timeout);
            catalog_profile_peer_t *peer = catalog_profile_peer(w->dest);

            if (peer) {
               peer->timeouts++;
            }
#endif
            rr_relay_sync_status(w->dest, false, false);
            Log(LOG_WARN, "relay.catalog", "Sync %s to %s timed out awaiting ack after %.3f s (%u descriptors)", (char *)key, rrserver_links_format_node(w->dest
               , dest_label, sizeof(dest_label)), (now - w->started) / 1000000.0, w->count);
            g_hash_table_iter_remove(&pending);
         }
      }
   }

   if (!relay_catalog_snapshots) {
      return;
   }
   GHashTableIter iter;
   gpointer key, value;
   g_hash_table_iter_init(&iter, relay_catalog_snapshots);
   while (g_hash_table_iter_next(&iter, &key, &value)) {
      relay_catalog_snapshot_t *s = value;

      if (now < s->next_send) {
         continue;
      }
      s->next_send = now + 50000;

      /* Remove through the active iterator to avoid invalidating it. */
      if (!snapshot_tick(s)) {
         g_hash_table_iter_remove(&iter);
      }

      return;
   }
}

/* Link generations must not share in-flight catalog transports. */
void rr_relay_catalog_peer_lost(const char *node) {
   if (!node || !*node) {
      return;
   }

   if (relay_catalog_snapshots && g_hash_table_remove(relay_catalog_snapshots, node)) {
      Log(LOG_INFO, "relay.catalog", "Cancelled outgoing catalog snapshot for disconnected peer %s", node);
   }

   if (relay_catalog_awaiting_ack) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, relay_catalog_awaiting_ack);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         catalog_ack_wait_t *w = value;

         if (strcmp(w->dest, node)) {
            continue;
         }
         Log(LOG_INFO, "relay.catalog", "Cancelled pending catalog acknowledgment %s for disconnected peer %s", (char *)key, node);
         g_hash_table_iter_remove(&iter);
      }
   }
}

void rr_relay_send_catalogs(const char *dest) {
   if (!dest || !*dest) {
      return;
   }
   char dest_label[192];
   rrserver_links_format_node(dest, dest_label, sizeof(dest_label));

   if (!relay_catalog_snapshots) {
      relay_catalog_snapshots = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, snapshot_free);
   }

   /* A reconnect invalidates the previous pending snapshot and acknowledgments. */
   if (relay_catalog_awaiting_ack) {
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, relay_catalog_awaiting_ack);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
         catalog_ack_wait_t *w = value;

         if (!strcmp(w->dest, dest)) {
#ifdef USE_PROFILING
            CP_INC(ack_cancelled);
#endif
            g_hash_table_iter_remove(&iter);
         }
      }
   }
#ifdef USE_PROFILING

   if (g_hash_table_lookup(relay_catalog_snapshots, dest)) {
      CP_INC(superseded);
   }
   const gint64 build_started = g_get_monotonic_time();
   CP_INC(snapshots);
   catalog_profile_peer_t *profile_peer = catalog_profile_peer(dest);

   if (profile_peer) {
      profile_peer->snapshots++;
   }
#endif
   g_hash_table_remove(relay_catalog_snapshots, dest);
   relay_catalog_snapshot_t *s = g_new0(relay_catalog_snapshot_t, 1);
   s->dest = g_strdup(dest);
   s->sync_id = g_uuid_string_random();
   /* Outbound snapshot replaces the previously acknowledged catalog view. */
   rr_relay_sync_status(dest, false, false);
   s->messages = g_ptr_array_new_with_free_func((GDestroyNotify) dict_free);
   const char *types[] = {
      "node", "rig", "vfo", "room", "service"
   };

   /* Root objects, then root services, then nested MENU services. */
   for (unsigned i = 0 ; i < 5 ; i++) {
      for (unsigned pass = 0 ; pass < (i == 4 ? 2u : 1u) ; pass++) {
         for (struct catalog *entry = rr_relay_catalogs ; entry ; entry = entry->next) {
            if (strcmp(dict_get(entry->descriptor, "object.type", "room"), types[i])) {
               continue;
            }

            if (i == 4) {
               bool nested = !strncmp(dict_get(entry->descriptor, "catalog.parent", ""), "service:", 8);

               if (nested != (pass == 1)) {
                  continue;
               }
            }
            if (!dict_get(entry->descriptor, "identity.signature", NULL)) {
               Log(LOG_WARN, "mesh.identity", "Unsigned catalog descriptor queued for %s from %s: %s",
                  dest, entry->owner, cfg_get_bool("cluster.force-signed-catalog", true) ? "skipped" : "legacy mode permitted");
               if (cfg_get_bool("cluster.force-signed-catalog", true)) continue;
            }
            const char *route = rrserver_links_route(entry->owner);

            if (route && !strcmp(route, dest)) {
               continue;
            }
            char *json = dict2json(entry->descriptor);

            if (!json) {
               continue;
            }
            dict *d = dict_new();

            if (d) {
               dict_add(d, "op", "link.application");
               dict_add(d, "node", entry->owner);
               dict_add(d, "dest", dest);
               dict_add(d, "kind", "catalog");
               dict_add(d, "session", s->sync_id);
               dict_add(d, "username", "");
               dict_add(d, "payload", json);
               g_ptr_array_add(s->messages, d);
#ifdef USE_PROFILING
               CP_ADD(prepared_bytes, strlen(json));
#endif
            }
            free(json);
         }
      }
   }

   /* Start marker allows the receiver to count accepted and rejected items. */
   dict *begin = dict_new();

   if (begin) {
      dict *meta = dict_new();
      dict_add_ulong(meta, "catalog.expected", s->messages->len);
      char *payload = dict2json(meta);
      dict_free(meta);
      dict_add(begin, "op", "link.application");
      dict_add(begin, "node", rrserver_links_node());
      dict_add(begin, "dest", dest);
      dict_add(begin, "kind", "catalog-begin");
      dict_add(begin, "session", s->sync_id);
      dict_add(begin, "username", "");
      dict_add(begin, "payload", payload ? payload : "{}");
      free(payload);
      g_ptr_array_insert(s->messages, 0, begin);
   }
#ifdef USE_PROFILING
   CP_ADD(prepared, s->messages->len - (begin != NULL));
   catalog_profile_duration((guint64) MAX(0, g_get_monotonic_time() - build_started), "build");
#endif
   Log(LOG_INFO, "relay.catalog", "Queued %u catalog descriptors for paced delivery to %s (sync %s)", s->messages->len - (begin != NULL), dest_label, s->sync_id
      );
   g_hash_table_insert(relay_catalog_snapshots, s->dest, s);
   s->started_at = g_get_monotonic_time();
   s->next_send = s->started_at;
}

/* Defer catalog rebuilding after a local model change. */
void rr_relay_dirty(const char *event, const char *data, rrconn_t *client, void *user) {
   rr_relay_catalog_dirty = true;
}

/* Reconcile dirty discovery and announce additions/removals to peers. */
void rr_relay_refresh_catalog(void) {
#ifdef USE_PROFILING
   const gint64 refresh_started = g_get_monotonic_time();
   CP_INC(refreshes);
#endif
   /* Snapshot old descriptors so ordinary dirty refreshes transmit deltas, not hundreds of unchanged services on every poll. */
   GHashTable *previous = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

   if (!previous) {
      return;
   }

   for (struct catalog *entry = rr_relay_catalogs ; entry ; entry = entry->next) {
      if (!rr_relay_local_owner(entry->owner)) {
         continue;
      }
      char *json = dict2json(entry->descriptor);

      if (json) {
         g_hash_table_replace(previous, g_strdup(entry->key), json);
      }
   }

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
               CP_INC(delta_removes);
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

   /* New or modified local descriptors only; initial link readiness still uses rr_relay_send_catalogs() for a complete parent-first snapshot. */
   for (unsigned pass = 0 ; pass < 5 ; pass++) {
      for (struct catalog *entry = rr_relay_catalogs ; entry ; entry = entry->next) {
         if (!rr_relay_local_owner(entry->owner)) {
            continue;
         }
         const char *type = dict_get(entry->descriptor, "object.type", "room");
         unsigned stage = !strcmp(type, "node") ? 0 : !strcmp(type, "rig") ? 1 : !strcmp(type, "vfo") ? 2 : !strcmp(type, "room") ? 3 : 4;

         if (stage != pass) {
            continue;
         }
         char *json = dict2json(entry->descriptor);

         if (!json) {
            continue;
         }
         const char *old = g_hash_table_lookup(previous, entry->key);

         if (!old || strcmp(old, json)) {
            for (struct catalog *peer = rr_relay_catalogs ; peer ; peer = peer->next) {
               const char *next = rrserver_links_route(peer->owner);

               if (next && !strcmp(next, peer->owner) && !strcmp(dict_get(peer->descriptor, "object.type", ""), "node")) {
                  rr_relay_send_owned(next, entry->owner, "catalog", entry->descriptor);
                  CP_INC(delta_updates);
               }
            }
         }
         free(json);
      }
   }

   g_hash_table_destroy(previous);
#ifdef USE_PROFILING
   catalog_profile_duration((guint64) MAX(0, g_get_monotonic_time() - refresh_started), "refresh");
#endif
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
      const char *model = dict_get(c->descriptor, "object.model", NULL);

      if (model) {
         dict_add(d, "inventory.model", model);
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

      if (access && strcmp(access, "read-only") && strcmp(access, "read/write") && (!client->user || !has_priv(client->user->uid, access))) {
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

      /* Nested MENU items refer to the MENU service key as their parent. Expose that key as the MENU row's stable inventory identity. */
      if (!strcmp(kind, "rig-menu")) {
         dict_add(row, "inventory.uuid", c->key);
      }

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

         for (struct catalog *menu = rr_relay_catalogs ; menu ; menu = menu->next) {
            if (!strcmp(menu->owner, site->owner) &&
               !strcmp(dict_get(menu->descriptor, "catalog.parent", ""), id) && !strcmp(dict_get(menu->descriptor, "inventory.kind", ""), "rig-menu")) {
               inventory_services(client, request, site->owner, dict_get(menu->descriptor, "catalog.service", ""), 3);
            }
         }

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

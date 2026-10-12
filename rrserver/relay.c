#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/wire.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <rrserver/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/database.h>
#include <rrserver/discovery.h>
extern struct GlobalState rig;
#define MAX_CATALOG 2048
#define MAX_RELAY_SESSIONS 128
#define MAX_SUBSCRIPTIONS 512
struct catalog {
   struct catalog *next;
   char owner[37], key[160];
   dict *descriptor;
   bool seen;
};
struct relay_client {
   struct relay_client *next;
   rrconn_t *client;
   char id[37];
};
struct remote_session {
   struct remote_session *next;
   char origin[37], id[37];
   rrconn_t client;
};
struct subscription {
   struct subscription *next;
   char owner[37], room[128], node[37];
};
struct directory {
   struct directory *next;
   char node[37];
   dict *users;
};
static struct directory *directories;
static struct catalog *catalogs;
static struct relay_client *clients;
static struct remote_session *remotes;
static struct subscription *subscriptions;
static size_t catalog_count, client_count, remote_count, subscription_count;
static bool initialized, applying, catalog_dirty;

static dict *copy_dict(dict *d) {
   char *json = dict2json(d);
   dict *copy = json ? json2dict(json) : NULL;
   free(json);
   return copy;
}
static bool packet(const char *dest, const char *kind, const char *session, const char *username, dict *payload) {
   char *json = dict2json(payload);
   dict *d = dict_new();
   if (!d || !json) { dict_free(d); free(json); return false; }
   dict_add(d, "op", "link.application");
   dict_add(d, "node", rrserver_links_node());
   dict_add(d, "dest", dest);
   dict_add(d, "kind", kind);
   dict_add(d, "session", session ? session : "");
   dict_add(d, "username", username ? username : "");
   dict_add(d, "payload", json);
   bool ok = rrserver_links_send(dest, d);
   dict_free(d);
   free(json);
   return ok;
}
static void send_owned(const char *dest, const char *owner, const char *kind, dict *payload) {
   char *json = dict2json(payload);
   dict *d = dict_new();
   if (!d || !json) { dict_free(d); free(json); return; }
   dict_add(d, "op", "link.application"); dict_add(d, "node", owner);
   dict_add(d, "dest", dest); dict_add(d, "kind", kind);
   dict_add(d, "session", ""); dict_add(d, "username", ""); dict_add(d, "payload", json);
   rrserver_links_send(dest, d); dict_free(d); free(json);
}
static void directory_put(const char *node, dict *d) {
   unsigned count = dict_get_uint(d, "count", HTTP_MAX_USERS + 1);
   if (count > HTTP_MAX_USERS) return;
   for (unsigned i = 0; i < count; i++) {
      char key[64]; snprintf(key, sizeof(key), "user.%u.name", i);
      const char *name = dict_get(d, key, NULL);
      if (!name || !*name || strlen(name) > HTTP_USER_LEN) return;
   }
   struct directory *entry = directories;
   unsigned entries = 0;
   for (; entry; entry = entry->next) { entries++; if (!strcmp(entry->node, node)) break; }
   if (!entry) {
      if (entries >= 256) return;
      entry = calloc(1, sizeof(*entry));
      if (!entry) return;
      strcpy(entry->node, node); entry->next = directories; directories = entry;
   }
   dict_free(entry->users); entry->users = copy_dict(d);
}
static dict *local_users(void) {
   dict *d = dict_new();
   if (!d) return NULL;
   unsigned counts[HTTP_MAX_USERS] = {0}, n = 0;
   for (rrconn_t *c = http_client_list; c; c = c->next) {
      if (c->authenticated && c->is_ws && c->active && c->user && !client_has_flag(c, FLAG_VIDEO_SOURCE) && c->user->uid < HTTP_MAX_USERS) counts[c->user->uid]++;
   }
   for (unsigned i = 0; i < HTTP_MAX_USERS; i++) if (counts[i]) {
      char key[64]; snprintf(key, sizeof(key), "user.%u.name", n); dict_add(d, key, http_users[i].name);
      snprintf(key, sizeof(key), "user.%u.sessions", n++); dict_add_uint(d, key, counts[i]);
   }
   dict_add_uint(d, "count", n); return d;
}
static void send_users(rrconn_t *client, const char *room) {
   // No credentials, addresses or peer-side privileges are imported here.
   dict *local = local_users();
   directory_put(rrserver_links_node(), local); dict_free(local);
   unsigned count = 0;
   for (struct directory *entry = directories; entry; entry = entry->next) {
      struct catalog *site = NULL;
      for (struct catalog *c = catalogs; c; c = c->next) if (!strcmp(c->key, entry->node)) site = c;
      const char *alias = site ? dict_get(site->descriptor, "object.alias", entry->node) : cfg_get("station.name");
      for (unsigned i = 0; i < dict_get_uint(entry->users, "count", 0); i++) {
         char key[64], text[384]; snprintf(key, sizeof(key), "user.%u.name", i);
         const char *name = dict_get(entry->users, key, "?");
         snprintf(key, sizeof(key), "user.%u.sessions", i);
         snprintf(text, sizeof(text), "%s@%s (%u sessions)", name, alias, dict_get_uint(entry->users, key, 0));
         dict *d = dict_new(); if (!d) continue;
         dict_add(d, "msg.type", "talk"); dict_add(d, "talk.cmd", "msg"); dict_add(d, "talk.msg_type", "notice");
         dict_add(d, "talk.target", room ? room : ws_site_room()); dict_add(d, "talk.from", "***SERVER***");
         dict_add(d, "talk.data", text); ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT); dict_free(d); count++;
      }
   }
   dict *end = dict_new();
   if (end) {
      dict_add(end, "msg.type", "talk"); dict_add(end, "talk.cmd", "msg"); dict_add(end, "talk.msg_type", "notice");
      dict_add(end, "talk.target", room ? room : ws_site_room()); dict_add(end, "talk.from", "***SERVER***");
      dict_add(end, "talk.data", count ? "End of users" : "No users online");
      ws_send_dict(NULL, client, end, WEBSOCKET_OP_TEXT); dict_free(end);
   }
}
static bool local_owner(const char *node) { return !strcmp(node, rrserver_links_node()); }
static struct remote_session *remote_for(rrconn_t *client) {
   for (struct remote_session *r = remotes; r; r = r->next) if (&r->client == client) return r;
   return NULL;
}
static struct catalog *find_catalog(const char *key) {
   for (struct catalog *c = catalogs; c; c = c->next) if (!strcmp(c->key, key)) return c;
   return NULL;
}
static struct catalog *room_catalog(const char *room) {
   if (!room) return NULL;
   char key[160];
   snprintf(key, sizeof(key), "room:%s", room);
   return find_catalog(key);
}
static const char *message_room(dict *d) {
   const char *room = dict_get(d, "cat.room", dict_get(d, "talk.room", dict_get(d, "request.room", NULL)));
   if (!room) {
      const char *target = dict_get(d, "talk.target", NULL);
      if (target && (*target == '#' || *target == '&')) room = target;
   }
   if (!room) {
      struct catalog *c = find_catalog(dict_get(d, "target", ""));
      if (c) room = dict_get(c->descriptor, "object.room", NULL);
   }
   return room;
}
static void catalog_put(const char *owner, dict *d) {
   const char *uuid = dict_get(d, "object.uuid", NULL), *room = dict_get(d, "catalog.room", NULL);
   char key[160];
   if (uuid && !local_owner(owner)) {
      const char *type = dict_get(d, "object.type", ""), *parent = dict_get(d, "object.owner", "");
      if (!strcmp(type, "node")) { if (strcmp(uuid, owner)) return; }
      else if (!strcmp(type, "rig")) { if (strcmp(parent, owner)) return; }
      else if (!strcmp(type, "vfo")) {
         struct catalog *rig = find_catalog(parent);
         if (!rig || strcmp(rig->owner, owner) || strcmp(dict_get(rig->descriptor, "object.type", ""), "rig")) return;
      } else return;
   }
   if (uuid && rr_object_uuid_valid(uuid)) snprintf(key, sizeof(key), "%s", uuid);
   else if (room && ws_room_name_valid(room)) snprintf(key, sizeof(key), "room:%s", room);
   else return;
   struct catalog *c = find_catalog(key);
   if (c && strcmp(c->owner, owner)) return; // Never replace another owner's identity.
   dict *copy = copy_dict(d);
   if (!copy) return;
   if (!c) {
      if (catalog_count == MAX_CATALOG) { dict_free(copy); return; }
      c = calloc(1, sizeof(*c));
      if (!c) { dict_free(copy); return; }
      snprintf(c->key, sizeof(c->key), "%s", key);
      snprintf(c->owner, sizeof(c->owner), "%s", owner);
      c->next = catalogs; catalogs = c; catalog_count++;
   }
   dict_free(c->descriptor); c->descriptor = copy; c->seen = true;
}
static void add_object(const char *uuid, const char *type, const char *owner, const char *alias, const char *room) {
   dict *d = dict_new();
   if (!d) return;
   dict_add(d, "msg.type", "object"); dict_add(d, "object.cmd", "descriptor");
   dict_add(d, "object.uuid", uuid); dict_add(d, "object.type", type);
   if (owner) dict_add(d, "object.owner", owner);
   dict_add(d, "object.alias", alias); dict_add(d, "object.name", alias);
   dict_add(d, "object.room", room); dict_add(d, "object.lifecycle", "persistent");
   catalog_put(rrserver_links_node(), d); dict_free(d);
}
static bool collect_vfo(rr_server_vfo_t *vfo, void *user) {
   rr_server_rig_t *radio = user;
   add_object(rr_server_vfo_id(vfo), "vfo", rr_server_rig_id(radio), rr_server_vfo_alias(vfo), rr_rig_registry_room(rig.rigs, radio));
   return false;
}
static bool collect_rig(rr_server_rig_t *radio, void *user) {
   (void)user;
   add_object(rr_server_rig_id(radio), "rig", rrserver_links_node(), rr_rig_registry_alias(rig.rigs, radio), rr_rig_registry_room(rig.rigs, radio));
   rr_server_vfo_foreach(radio, collect_vfo, radio);
   return false;
}
static void collect_local(void) {
   if (!*rrserver_links_node() || !rig.rigs) return;
   add_object(rrserver_links_node(), "node", NULL, cfg_get("station.name"), ws_site_room());
   rr_rig_registry_foreach(rig.rigs, collect_rig, NULL);
#ifdef USE_SQLITE
   char *rooms = db_room_list(masterdb), *save = NULL;
   for (char *room = rooms ? strtok_r(rooms, " ", &save) : NULL; room; room = strtok_r(NULL, " ", &save)) {
      dict *d = dict_new();
      if (!d) continue;
      dict_add(d, "catalog.room", room);
      catalog_put(rrserver_links_node(), d); dict_free(d);
   }
   free(rooms);
#endif
}
static void send_catalogs(const char *dest) {
   const char *types[] = { "node", "rig", "vfo", "room" };
   for (unsigned i = 0; i < 4; i++) for (struct catalog *c = catalogs; c; c = c->next) {
      if (strcmp(dict_get(c->descriptor, "object.type", "room"), types[i])) continue;
      // Forward the actual owning identity, not our intermediate identity.
      const char *route = rrserver_links_route(c->owner);
      if (route && !strcmp(route, dest)) continue;
      char *json = dict2json(c->descriptor);
      dict *d = dict_new();
      if (!json || !d) { free(json); dict_free(d); continue; }
      dict_add(d, "op", "link.application"); dict_add(d, "node", c->owner);
      dict_add(d, "dest", dest); dict_add(d, "kind", "catalog");
      dict_add(d, "session", ""); dict_add(d, "username", ""); dict_add(d, "payload", json);
      rrserver_links_send(dest, d); dict_free(d); free(json);
   }
}
static struct relay_client *client_for(rrconn_t *client, bool create) {
   for (struct relay_client *c = clients; c; c = c->next) if (c->client == client) return c;
   if (!create || client_count == MAX_RELAY_SESSIONS) return NULL;
   struct relay_client *c = calloc(1, sizeof(*c));
   char *id = g_uuid_string_random();
   if (!c || !id) { free(c); g_free(id); return NULL; }
   c->client = client; strcpy(c->id, id); g_free(id);
   c->next = clients; clients = c; client_count++;
   return c;
}
static bool demand(const char *owner, const char *room) {
   for (struct relay_client *c = clients; c; c = c->next) if (ws_client_in_room(c->client, room)) return true;
   for (struct subscription *s = subscriptions; s; s = s->next) if (!strcmp(s->owner, owner) && !strcmp(s->room, room)) return true;
   return false;
}
static void subscribe_upstream(const char *owner, const char *room, bool subscribe) {
   if (local_owner(owner)) return;
   dict *d = dict_new();
   if (!d) return;
   dict_add(d, "room", room);
   packet(owner, subscribe ? "subscribe" : "unsubscribe", NULL, NULL, d);
   dict_free(d);
}
static void publish_state(const char *owner, const char *room, dict *d) {
   for (struct subscription *s = subscriptions; s; s = s->next) {
      if (!strcmp(s->owner, owner) && !strcmp(s->room, room)) {
         packet(s->node, "state", NULL, owner, d);
         Log(LOG_DEBUG, "relay", "State %s %s room %s to branch %s", dict_get(d, "msg.type", ""),
            dict_get(d, "cat.cmd", dict_get(d, "property.cmd", dict_get(d, "talk.cmd", ""))), room, s->node);
      }
   }
}
static bool permitted_message(dict *d) {
   const char *family = dict_get(d, "msg.type", ""), *cmd = dict_get(d, "talk.cmd", "");
   if (!strcmp(family, "cat")) return strcmp(dict_get(d, "cat.cmd", ""), "ptt") != 0;
   if (!strcmp(family, "property")) return !strcmp(dict_get(d, "property.cmd", ""), "set") && strcmp(dict_get(d, "property.name", ""), "ptt");
   return !strcmp(family, "talk") && (!strcmp(cmd, "join") || !strcmp(cmd, "part") || !strcmp(cmd, "msg") || !strcmp(cmd, "topic"));
}
static void route_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (length != sizeof(rr_server_io_t) || !client || applying || remote_for(client)) return;
   rr_server_io_t *io = (rr_server_io_t *)data;
   if (!strcmp(dict_get(io->message, "msg.type", ""), "talk") && !strcmp(dict_get(io->message, "talk.cmd", ""), "users")) {
      io->handled = true; io->result = true;
      if (client->authenticated) send_users(client, dict_get(io->message, "talk.target", NULL));
      return;
   }
   const char *room = message_room(io->message);
   struct catalog *c = room_catalog(room);
   if (!c || local_owner(c->owner)) return;
   io->handled = true;
   if (!client->authenticated || !client->user || !permitted_message(io->message)) {
      ws_send_error(client, "This remote command is not supported yet (media and remote PTT are deferred)"); return;
   }
   struct relay_client *origin = client_for(client, true);
   if (!origin || !packet(c->owner, "request", origin->id, client->user->name, io->message)) {
      ws_send_error(client, "Remote owner is unavailable"); return;
   }
   io->result = true;
}
static void send_remote(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event; (void)user;
   if (length != sizeof(rr_server_io_t)) return;
   struct remote_session *r = remote_for(client);
   if (!r) return;
   rr_server_io_t *io = (rr_server_io_t *)data;
   io->handled = true;
   const char *family = dict_get(io->message, "msg.type", "");
   // Media announcements do not make a remote channel usable.
   if (!strcmp(family, "media")) { io->result = true; return; }
   io->result = packet(r->origin, "reply", r->id, NULL, io->message);
}
static void publish(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   if (applying || length != sizeof(rr_server_publish_t)) return;
   const rr_server_publish_t *p = data;
   const char *room = p->room ? p->room : message_room(p->message);
   struct catalog *c = room_catalog(room);
   if (c && local_owner(c->owner)) publish_state(c->owner, room, p->message);
}
static void roster(const char *event, const char *room, rrconn_t *client, void *user) {
   (void)event; (void)user;
   for (struct remote_session *r = remotes; r; r = r->next) if (ws_client_in_room(&r->client, room)) ws_send_userinfo_room(&r->client, client, room);
}
static struct remote_session *owner_session(const char *origin, const char *id, const char *username) {
   if (!rr_object_uuid_valid(id)) return NULL;
   int uid = http_getuid(username);
   if (uid < 0 || uid >= HTTP_MAX_USERS || !http_users[uid].enabled ||
      http_users[uid].password_change_required || (http_users[uid].password_expires && http_users[uid].password_expires <= now)) return NULL;
   for (struct remote_session *r = remotes; r; r = r->next) {
      if (!strcmp(r->origin, origin) && !strcmp(r->id, id)) {
         if (strcasecmp(r->client.chatname, username)) return NULL;
         r->client.user = &http_users[uid]; return r;
      }
   }
   if (remote_count == MAX_RELAY_SESSIONS) return NULL;
   struct remote_session *r = calloc(1, sizeof(*r));
   if (!r) return NULL;
   strcpy(r->origin, origin); strcpy(r->id, id);
   r->client.authenticated = r->client.active = r->client.is_ws = true;
   r->client.user = &http_users[uid];
   snprintf(r->client.chatname, sizeof(r->client.chatname), "%s", username);
   snprintf(r->client.token, sizeof(r->client.token), "%s", id);
   r->next = remotes; remotes = r; remote_count++;
   return r;
}
static void execute(const char *origin, const char *id, const char *username, dict *d) {
   struct remote_session *r = owner_session(origin, id, username);
   const char *room = message_room(d);
   struct catalog *c = room_catalog(room);
   if (!r || !c || !local_owner(c->owner) || !permitted_message(d)) {
      dict *error = dict_new();
      if (error) { dict_add(error, "msg.type", "error"); dict_add(error, "error.msg", "Remote owner requires an enabled local account and an allowed command"); packet(origin, "reply", id, NULL, error); dict_free(error); }
      return;
   }
   char *wire = rr_wire_encode(d);
   if (!wire) { ws_send_error(&r->client, "Invalid remote client message"); return; }
   free(wire);
   bool ok = ws_process_dict(&r->client, d);
   const char *cmd = dict_get(d, "talk.cmd", "");
   if (ok && !strcmp(cmd, "join") && ws_client_in_room(&r->client, room)) {
      dict *joined = dict_new();
      if (joined) {
         dict_add(joined, "msg.type", "talk"); dict_add(joined, "talk.cmd", "join");
         dict_add(joined, "talk.room", room); dict_add(joined, "talk.target", room);
         dict_add(joined, "talk.user", r->client.chatname); dict_add(joined, "talk.session", r->id);
         dict_add_bool(joined, "room.has-vfos", ws_room_has_vfos(room));
         dict_add_bool(joined, "room.tx-control", ws_room_tx_control(room));
         dict_add_ulong(joined, "room.vfo-mask", ws_room_vfo_mask(room));
         dict_add_bool(joined, "room.rx-tunable", ws_room_rx_tunable(room));
         dict_add_ulong(joined, "room.rx-tuning-mask", ws_room_rx_tuning_mask(room));
         packet(origin, "reply", id, NULL, joined); dict_free(joined);
      }
      rrserver_objects_room_state(&r->client, room);
   }
}
static void update_membership(rrconn_t *client, const char *room, bool joined) {
   if (joined) {
      if (!ws_client_in_room(client, room) && strlen(client->rooms) + strlen(room) + 2 < sizeof(client->rooms)) {
         if (*client->rooms) strlcat(client->rooms, ",", sizeof(client->rooms));
         strlcat(client->rooms, room, sizeof(client->rooms));
      }
   } else {
      char copy[AUTOJOIN_LEN], out[AUTOJOIN_LEN] = "", *save = NULL;
      snprintf(copy, sizeof(copy), "%s", client->rooms);
      for (char *r = strtok_r(copy, ",", &save); r; r = strtok_r(NULL, ",", &save)) {
         if (!strcasecmp(r, room)) continue;
         if (*out) strlcat(out, ",", sizeof(out));
         strlcat(out, r, sizeof(out));
      }
      snprintf(client->rooms, sizeof(client->rooms), "%s", out);
   }
}
static void deliver(rrconn_t *client, dict *d) {
   const char *family = dict_get(d, "msg.type", "");
   if (!strcmp(family, "object") || !strcmp(family, "property")) rrserver_objects_remote(client, d);
   else { applying = true; ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT); applying = false; }
}
static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   dict *envelope = json2dict(data);
   if (!envelope) return;
   const char *origin = dict_get(envelope, "node", ""), *dest = dict_get(envelope, "dest", ""),
      *kind = dict_get(envelope, "kind", ""), *id = dict_get(envelope, "session", "");
   dict *d = json2dict(dict_get(envelope, "payload", ""));
   if (!d) goto done;
   if (!strcmp(kind, "subscribe") || !strcmp(kind, "unsubscribe")) {
      const char *room = dict_get(d, "room", "");
      struct catalog *c = room_catalog(room);
      const char *from = rrserver_links_route(origin), *toward = rrserver_links_route(dest);
      if (!c || strcmp(c->owner, dest) || !from || strcmp(from, origin) ||
         (toward && !strcmp(from, toward))) goto done;
      bool before = demand(dest, room);
      struct subscription **p = &subscriptions;
      while (*p && (strcmp((*p)->owner, dest) || strcmp((*p)->room, room) || strcmp((*p)->node, origin))) p = &(*p)->next;
      if (!strcmp(kind, "subscribe") && !*p && subscription_count < MAX_SUBSCRIPTIONS) {
         struct subscription *s = calloc(1, sizeof(*s));
         if (s) { strcpy(s->owner, dest); snprintf(s->room, sizeof(s->room), "%s", room); strcpy(s->node, origin); s->next = subscriptions; subscriptions = s; subscription_count++; }
      } else if (!strcmp(kind, "unsubscribe") && *p) { struct subscription *s = *p; *p = s->next; free(s); subscription_count--; }
      bool after = demand(dest, room);
      if (before != after) subscribe_upstream(dest, room, after);
   } else if (!local_owner(dest)) {
      rrserver_links_send(dest, envelope);
   } else if (!strcmp(kind, "catalog") || !strcmp(kind, "catalog-remove") || !strcmp(kind, "users")) {
      if (!strcmp(kind, "catalog")) {
         catalog_put(origin, d);
         if (dict_get(d, "object.uuid", NULL)) {
            dict *added = copy_dict(d);
            if (added) { dict_add(added, "object.cmd", "added"); rrserver_objects_remote(NULL, added); dict_free(added); }
         }
      }
      else if (!strcmp(kind, "users")) directory_put(origin, d);
      else {
         const char *uuid = dict_get(d, "object.uuid", NULL), *room = dict_get(d, "catalog.room", NULL);
         char key[160]; snprintf(key, sizeof(key), "%s%s", uuid ? "" : "room:", uuid ? uuid : room ? room : "");
         struct catalog **p = &catalogs;
         while (*p) {
            struct catalog *entry = *p;
            if (!strcmp(entry->key, key) && !strcmp(entry->owner, origin)) {
               if (uuid) { dict_add(d, "msg.type", "object"); dict_add(d, "object.cmd", "removed"); rrserver_objects_remote(NULL, d); }
               *p = entry->next; dict_free(entry->descriptor); free(entry); catalog_count--; break;
            } else p = &entry->next;
         }
      }
      // Each receiving branch forwards discovery to the other direct branches.
      for (struct catalog *c = catalogs; c; c = c->next) {
         if (strcmp(dict_get(c->descriptor, "object.type", ""), "node")) continue;
         const char *next = rrserver_links_route(c->owner), *from = rrserver_links_route(origin);
         if (next && !strcmp(next, c->owner) && from && strcmp(next, from)) {
            dict_add(envelope, "dest", next); rrserver_links_send(next, envelope);
         }
      }
   } else if (!strcmp(kind, "request")) {
      execute(origin, id, dict_get(envelope, "username", ""), d);
   } else if (!strcmp(kind, "reply")) {
      for (struct relay_client *c = clients; c; c = c->next) if (!strcmp(c->id, id)) {
         const char *cmd = dict_get(d, "talk.cmd", ""), *room = dict_get(d, "talk.room", NULL);
         if (room && (!strcmp(cmd, "join") || !strcmp(cmd, "part")) && !strcmp(dict_get(d, "talk.session", ""), id)) {
            bool before = demand(origin, room);
            update_membership(c->client, room, !strcmp(cmd, "join"));
            bool after = demand(origin, room);
            if (before != after) subscribe_upstream(origin, room, after);
            dict_add(d, "talk.session", c->client->token);
         }
         deliver(c->client, d); break;
      }
   } else if (!strcmp(kind, "state")) {
      const char *room = message_room(d);
      struct catalog *c = room_catalog(room);
      // State may come from an intermediate subscribed neighbor; its catalog
      // still identifies the only authority for this room.
      const char *authority = dict_get(envelope, "username", ""), *from = rrserver_links_route(origin);
      const char *toward = rrserver_links_route(authority);
      if (!c || !room || strcmp(c->owner, authority) || !from || !toward ||
         strcmp(from, toward) || !demand(c->owner, room)) goto done;
      for (struct relay_client *local = clients; local; local = local->next) if (ws_client_in_room(local->client, room)) deliver(local->client, d);
      publish_state(c->owner, room, d);
   } else if (!strcmp(kind, "close")) {
      struct remote_session **p = &remotes;
      while (*p) {
         struct remote_session *r = *p;
         if (!strcmp(r->origin, origin) && !strcmp(r->id, id)) { *p = r->next; free(r); remote_count--; } else p = &r->next;
      }
   }
done:
   dict_free(d); dict_free(envelope);
}
static void ready(const char *event, const char *node, rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   collect_local(); send_catalogs(node);
   dict *local = local_users();
   directory_put(rrserver_links_node(), local); dict_free(local);
   for (struct directory *entry = directories; entry; entry = entry->next) {
      const char *route = rrserver_links_route(entry->node);
      if (!route || strcmp(route, node)) send_owned(node, entry->node, "users", entry->users);
   }
}
static void close_client(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)data; (void)user;
   struct relay_client **p = &clients;
   while (*p) {
      struct relay_client *c = *p;
      if (c->client != client) { p = &c->next; continue; }
      char rooms[AUTOJOIN_LEN]; snprintf(rooms, sizeof(rooms), "%s", client->rooms);
      *p = c->next; client_count--;
      for (struct catalog *entry = catalogs; entry; entry = entry->next) {
         if (!strcmp(dict_get(entry->descriptor, "object.type", ""), "node") && !local_owner(entry->owner)) {
            dict *d = dict_new(); if (d) { packet(entry->owner, "close", c->id, NULL, d); dict_free(d); }
         }
      }
      char *save = NULL;
      for (char *room = strtok_r(rooms, ",", &save); room; room = strtok_r(NULL, ",", &save)) {
         struct catalog *entry = room_catalog(room);
         if (entry && !demand(entry->owner, room)) subscribe_upstream(entry->owner, room, false);
      }
      free(c);
   }
}
static void lost(const char *event, const char *node, rrconn_t *client, void *user) {
   (void)event; (void)node; (void)client; (void)user;
   struct subscription **sp = &subscriptions;
   while (*sp) {
      struct subscription *s = *sp;
      if ((!local_owner(s->owner) && !rrserver_links_route(s->owner)) || !rrserver_links_route(s->node)) {
         *sp = s->next; free(s); subscription_count--;
      } else sp = &s->next;
   }
   struct remote_session **rp = &remotes;
   while (*rp) {
      struct remote_session *r = *rp;
      if (!rrserver_links_route(r->origin)) { *rp = r->next; free(r); remote_count--; }
      else rp = &r->next;
   }
   struct directory **dp = &directories;
   while (*dp) {
      struct directory *entry = *dp;
      if (!local_owner(entry->node) && !rrserver_links_route(entry->node)) { *dp = entry->next; dict_free(entry->users); free(entry); }
      else dp = &entry->next;
   }
   struct catalog **cp = &catalogs;
   while (*cp) {
      struct catalog *c = *cp;
      if (local_owner(c->owner) || rrserver_links_route(c->owner)) { cp = &c->next; continue; }
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);
      if (room) for (struct relay_client *local = clients; local; local = local->next) {
         if (ws_client_in_room(local->client, room)) {
            update_membership(local->client, room, false);
            ws_send_error(local->client, "Owner of room %s disconnected; join again after it reconnects", room);
         }
      }
      const char *uuid = dict_get(c->descriptor, "object.uuid", NULL);
      if (uuid) {
         dict *removed = dict_new();
         if (removed) {
            dict_add(removed, "msg.type", "object"); dict_add(removed, "object.cmd", "removed");
            dict_add(removed, "object.uuid", uuid); rrserver_objects_remote(NULL, removed); dict_free(removed);
         }
      }
      *cp = c->next; dict_free(c->descriptor); free(c); catalog_count--;
   }
}
static void dirty(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)data; (void)client; (void)user; catalog_dirty = true;
}
static void refresh_catalog(void) {
   for (struct catalog *c = catalogs; c; c = c->next) if (local_owner(c->owner)) c->seen = false;
   collect_local();
   struct catalog **p = &catalogs;
   while (*p) {
      struct catalog *c = *p;
      if (local_owner(c->owner) && !c->seen) {
         for (struct catalog *peer = catalogs; peer; peer = peer->next) {
            const char *next = rrserver_links_route(peer->owner);
            if (next && !strcmp(next, peer->owner) && !strcmp(dict_get(peer->descriptor, "object.type", ""), "node")) send_owned(next, c->owner, "catalog-remove", c->descriptor);
         }
         *p = c->next; dict_free(c->descriptor); free(c); catalog_count--;
      } else p = &c->next;
   }
   for (struct catalog *c = catalogs; c; c = c->next) {
      const char *next = rrserver_links_route(c->owner);
      if (next && !strcmp(next, c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) send_catalogs(next);
   }
   catalog_dirty = false;
}
void rrserver_relay_poll(void) {
   if (!initialized && *rrserver_links_node()) { collect_local(); initialized = true; }
   if (!*rrserver_links_node()) return;
   if (catalog_dirty) refresh_catalog();
   dict *local = local_users();
   struct directory *entry = directories;
   while (entry && !local_owner(entry->node)) entry = entry->next;
   char *old = entry ? dict2json(entry->users) : NULL, *current = dict2json(local);
   if (current && (!old || strcmp(current, old))) {
      directory_put(rrserver_links_node(), local);
      for (struct catalog *c = catalogs; c; c = c->next) {
         const char *next = rrserver_links_route(c->owner);
         if (next && !strcmp(next, c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) send_owned(next, rrserver_links_node(), "users", local);
      }
   }
   free(old); free(current); dict_free(local);
}
void rrserver_relay_register_events(void) {
   event_on("room.add", dirty, NULL);
   event_on("room.delete", dirty, NULL);
   event_on("room.vfo", dirty, NULL);
   event_on("object.model.added", dirty, NULL);
   event_on("object.model.removed", dirty, NULL);
   event_on("object.model.schema", dirty, NULL);
   event_on("server.link.application", receive, NULL);
   event_on("server.link.ready", ready, NULL);
   event_on("server.link.lost", lost, NULL);
   event_on("serial.session.closed", close_client, NULL);
   event_on("protocol.object.close", close_client, NULL);
   event_on(RR_SERVER_ROSTER_EVENT, roster, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, route_request, NULL);
   event_on_binary(RR_SERVER_SEND_EVENT, send_remote, NULL);
   event_on_binary(RR_SERVER_PUBLISH_EVENT, publish, NULL);
}
char *rrserver_relay_remote_rooms(void) {
   GString *text = g_string_new("");
   if (!text) return NULL;
   for (struct catalog *c = catalogs; c; c = c->next) {
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);
      if (room && !local_owner(c->owner)) {
         if (text->len) g_string_append_c(text, ' ');
         g_string_append(text, room);
      }
   }
   return g_string_free(text, false);
}
char *rrserver_relay_servers(void) {
   GString *text = g_string_new(cfg_get("station.name"));
   if (!text) return NULL;
   for (struct catalog *c = catalogs; c; c = c->next) {
      if (!local_owner(c->owner) && !strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
         g_string_append(text, " ↗"); g_string_append(text, dict_get(c->descriptor, "object.alias", c->owner));
      }
   }
   return g_string_free(text, false);
}
char *rrserver_relay_rooms(const char *local) {
   GString *s = g_string_new(local ? local : "");
   if (!s) return NULL;
   for (struct catalog *c = catalogs; c; c = c->next) {
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);
      if (room && !local_owner(c->owner)) { if (s->len) g_string_append_c(s, ' '); g_string_append(s, room); }
   }
   return g_string_free(s, false);
}
void rrserver_relay_inventory(rrconn_t *client, const char *request) {
   for (struct catalog *c = catalogs; c; c = c->next) {
      if (local_owner(c->owner)) continue;
      const char *type = dict_get(c->descriptor, "object.type", "room"), *room = dict_get(c->descriptor, "catalog.room", NULL);
      dict *d = rr_inventory_row(request, 1, type, room ? room : dict_get(c->descriptor, "object.alias", ""), dict_get(c->descriptor, "object.uuid", NULL));
      if (d) { dict_add(d, "inventory.room", room ? room : dict_get(c->descriptor, "object.room", "")); rr_inventory_send(client, d); }
   }
}
void rrserver_relay_objects(rrconn_t *client, const char *request) {
   // Parent descriptors must precede their children in both native/browser caches.
   const char *types[] = { "node", "rig", "vfo" };
   for (unsigned i = 0; i < 3; i++) for (struct catalog *c = catalogs; c; c = c->next) {
      if (local_owner(c->owner) || strcmp(dict_get(c->descriptor, "object.type", ""), types[i])) continue;
      dict *d = copy_dict(c->descriptor);
      if (d) { dict_add(d, "request.id", request); rrserver_objects_remote(client, d); dict_free(d); }
   }
}
void rrserver_relay_fini(void) {
   while (directories) { struct directory *entry = directories; directories = entry->next; dict_free(entry->users); free(entry); }
   while (catalogs) { struct catalog *c = catalogs; catalogs = c->next; dict_free(c->descriptor); free(c); }
   while (clients) { struct relay_client *c = clients; clients = c->next; free(c); }
   while (remotes) { struct remote_session *r = remotes; remotes = r->next; free(r); }
   while (subscriptions) { struct subscription *s = subscriptions; subscriptions = s->next; free(s); }
   catalog_count = client_count = remote_count = subscription_count = 0; initialized = false;
}

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Application envelope routing, peer lifecycle and periodic reconciliation. */
#include "build_config.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <rrserver/relay.h>
#include <rrserver/media.direct.h>
#include <rrserver/supervision.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/media.health.h>
#ifdef USE_MONGOOSE
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
extern struct GlobalState rig;
#include <rrserver/relay.internal.h>
bool rr_relay_applying;
static bool rr_relay_initialized;
size_t rr_relay_subscription_count;
struct subscription *rr_relay_subscriptions;

/* Send a link envelope, adding the authenticated origin IP for requests. */
bool rr_relay_packet(const char *dest, const char *kind, const char *session, const char *username, dict *payload) {
   if (!strcmp(kind, "request")) {
      /* Referral hint supplied by the authenticated origin, never by client JSON. */
      dict_del(payload, "relay.ip");

      for (struct relay_client *c = rr_relay_clients ; c ; c = c->next) {
         if (session && !strcmp(c->id, session)) {
            if (*c->client->user_ip) {
               dict_add(payload, "relay.ip", c->client->user_ip);
            }
            break;
         }
      }
   }
   char *json = dict2json(payload);
   dict *d = dict_new();

   if (!d || !json) {
      dict_free(d);
      free(json);

      return false;
   }
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

/* Forward a link payload while retaining its authoritative owner. */
void rr_relay_send_owned(const char *dest, const char *owner, const char *kind, dict *payload) {
   char *json = dict2json(payload);
   dict *d = dict_new();

   if (!d || !json) {
      dict_free(d);
      free(json);

      return;
   }
   dict_add(d, "op", "link.application");
   dict_add(d, "node", owner);
   dict_add(d, "dest", dest);
   dict_add(d, "kind", kind);
   dict_add(d, "session", "");
   dict_add(d, "username", "");
   dict_add(d, "payload", json);
   rrserver_links_send(dest, d);
   dict_free(d);
   free(json);
}

/* File operations use correlated results, never generic chat errors, so transfers fail immediately rather than waiting for the client timeout. */
static void linked_file_error(rrconn_t *client, dict *request, const char *reason) {
   dict *reply = dict_new();

   if (!reply) {
      return;
   }
   dict_add(reply, "msg.type", "file");
   dict_add(reply, "file.cmd", "result");
   dict_add(reply, "file.path", dict_get(request, "file.path", "/"));
   dict_add(reply, "request.id", dict_get(request, "request.id", ""));
   dict_add(reply, "file.error", reason);
   ws_send_dict(NULL, client, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

/* Allow only supported client operations onto the application relay. */
bool rr_relay_permitted_message(dict *d) {
   const char *family = dict_get(d, "msg.type", ""), *cmd = dict_get(d, "talk.cmd", "");

   if (!strcmp(family, "file")) {
      const char *op = dict_get(d, "file.cmd", "");

      return !strcmp(op, "list") || !strcmp(op, "read") || !strcmp(op, "begin") ||
             !strcmp(op, "chunk") || !strcmp(op, "commit") || !strcmp(op, "cancel") || !strcmp(op, "mkdir") || !strcmp(op, "delete") || !strcmp(op, "rename");
   }

   if (!strcmp(family, "cat")) {
      return true;
   }

   if (!strcmp(family, "media")) {
      const char *cmd = dict_get(d, "media.cmd", "");

      return !strcmp(cmd, "subscribe") || !strcmp(cmd, "unsubscribe") || !strcmp(cmd, "codec") || !strcmp(cmd, "frame") || !strcmp(cmd, "feedback");
   }

   if (!strcmp(family, "property")) {
      return !strcmp(dict_get(d, "property.cmd", ""), "set") ||
             !strcmp(dict_get(d, "property.cmd", ""), "read") ||
             !strcmp(dict_get(d, "property.cmd", ""), "menu-set");
   }

   return !strcmp(family, "talk") && (!strcmp(cmd, "join") || !strcmp(cmd, "part") || !strcmp(cmd, "msg") || !strcmp(cmd, "topic")
      || !strcmp(cmd, "supervise"));
}

/* Intercept commands for remote rooms and route through their catalog owner. */
static void route_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (length != sizeof(rr_server_io_t) || !client || rr_relay_applying || rr_relay_remote_for(client)) {
      return;
   }
   rr_server_io_t *io = (rr_server_io_t *) data;

   if (!strcmp(dict_get(io->message, "msg.type", ""), "talk") && !strcmp(dict_get(io->message, "talk.cmd", ""), "users")) {
      io->handled = true;
      io->result = true;

      if (client->authenticated) {
         rr_relay_send_users(client, dict_get(io->message, "talk.target", NULL));
      }

      return;
   }

   /* /@<owner-node-uuid>/share/path is a virtual file namespace. The remote filesystem only ever sees /share/path. No room membership is necessary, but the
    * owner must authenticate the callsign itself. */
   if (!strcmp(dict_get(io->message, "msg.type", ""), "file")) {
      const char *path = dict_get(io->message, "file.path", "");

      if (strncmp(path, "/@", 2)) {
         return;                /* Local shares stay local. */
      }
      const char *end = strchr(path + 2, '/');
      size_t n = end ? (size_t)(end - path - 2) : strlen(path + 2);
      char owner[37];

      if (n != 36) {
         io->handled = true;
         linked_file_error(client, io->message, "Invalid linked share owner");

         return;
      }
      memcpy(owner, path + 2, n);
      owner[n] = 0;
      struct catalog *node = rr_relay_find_catalog(owner);
      io->handled = true;

      if (!client->authenticated || !client->user || !node ||
         strcmp(dict_get(node->descriptor, "object.type", ""), "node") ||
         strcmp(node->owner, owner) || rr_relay_local_owner(owner) || !rrserver_links_route(owner) || !rr_relay_permitted_message(io->message)) {
         linked_file_error(client, io->message, "Linked file owner unavailable or operation denied");

         return;
      }
      dict *request = dict_clone(io->message);

      if (!request) {
         return;
      }
      dict_add(request, "file.path", end ? end : "/");
      const char *dest = dict_get(request, "file.dest", NULL);

      if (dest) {
         char prefix[40];
         snprintf(prefix, sizeof(prefix), "/@%s", owner);

         if (strncmp(dest, prefix, strlen(prefix)) || (dest[strlen(prefix)] != '/' && dest[strlen(prefix)] != 0)) {
            dict_free(request);
            linked_file_error(client, io->message, "Cross-server file moves are not allowed");

            return;
         }
         char *local_dest = strdup(dest[strlen(prefix)] ? dest + strlen(prefix) : "/");

         if (!local_dest) {
            dict_free(request);

            return;
         }
         dict_add(request, "file.dest", local_dest);
         free(local_dest);
      }
      struct relay_client *origin = rr_relay_client_for(client, true);

      if (origin) {
         dict_add_ulong(request, "relay.deadline", now + 30);
         io->result = rr_relay_packet(owner, "request", origin->id, client->user->name, request);
      }

      if (!io->result) {
         linked_file_error(client, io->message, "Linked file owner unreachable");
      }
      dict_free(request);

      return;
   }

   /* Admin commands target exactly one node; '*' belongs to grant subject_host matching, never to command routing. */
   if (!strcmp(dict_get(io->message, "msg.type", ""), "talk") &&
      !strcmp(dict_get(io->message, "talk.cmd", ""), "admin") && *dict_get(io->message, "talk.server", "")) {
      const char *dest = dict_get(io->message, "talk.server", "");
      io->handled = true;

      if (!client->authenticated || !client->user ||
         !has_priv(client->user->uid, "admin|owner") || !rr_object_uuid_valid(dest) || rr_relay_local_owner(dest) || !rrserver_links_route(dest)) {
         ws_send_error(client, "Remote administration destination unavailable or denied");

         return;
      }
      /* Route toward any reachable target. Only the destination may apply remote-admin.next-hop-only to the authenticated origin. */
      struct relay_client *origin = rr_relay_client_for(client, true);
      dict *request = origin ? dict_clone(io->message) : NULL;

      if (request) {
         dict_del(request, "talk.server");
         dict_add_ulong(request, "relay.deadline", time(NULL) + 30);
      }
      io->result = request && rr_relay_packet(dest, "request", origin->id, client->user->name, request);
      dict_free(request);

      if (!io->result) {
         ws_send_error(client, "Unable to forward remote administration request");
      }

      return;
   }
   struct relay_media *media = rr_relay_media_map(client, dict_get(io->message, "media.chan-uuid", ""), false);

   if (!media && !strcmp(dict_get(io->message, "media.cmd", ""), "feedback")) {
      unsigned stream = dict_get_ulong(io->message, "media.stream", 0);

      for (struct relay_media *m = rr_relay_media_maps ; m ; m = m->next) {
         if (m->client == client && m->stream == stream && m->subscribed) {
            media = m;
            break;
         }
      }
   }

   if (media && !strcmp(dict_get(io->message, "media.cmd", ""), "subscribe")) {
      media->home = !strcmp(dict_get(io->message, "media.transport", ""), "rtp-home");
   }

   if (media) {
      dict_add(io->message, "media.room", media->room);
      dict_add(io->message, "media.chan-uuid", media->uuid);
   }
   const char *room = rr_relay_message_room(io->message);
   struct catalog *c = rr_relay_room_catalog(room);
   struct relay_client *known = rr_relay_client_for(client, false);
   struct wanted_room *wanted = known ? known->wanted : NULL;
   while (wanted && (!room || strcmp(wanted->room, room))) {
      wanted = wanted->next;
   }

   if (wanted && !ws_client_in_room(client, room) && !strcmp(dict_get(io->message, "talk.cmd", ""), "part")) {
      rr_relay_wanted_set(known, wanted->owner, room, false);
      dict *part = dict_new();

      if (part) {
         dict_add(part, "msg.type", "talk");
         dict_add(part, "talk.cmd", "part");
         dict_add(part, "talk.room", room);
         dict_add(part, "talk.user", client->chatname);
         dict_add(part, "talk.session", client->token);
         ws_send_dict(NULL, client, part, WEBSOCKET_OP_TEXT);
         dict_free(part);
      }
      io->handled = io->result = true;

      return;
   }

   if (!c) {
      if (wanted) {
         io->handled = true;
         ws_send_error(client, "Owner of %s is unavailable", room);
      }

      return;
   }

   if (rr_relay_local_owner(c->owner)) {
      return;
   }
   io->handled = true;

   if (!client->authenticated || !client->user || !rr_relay_permitted_message(io->message)) {
      const char *family = dict_get(io->message, "msg.type", "unknown");
      const char *command = !strcmp(family, "property") ?
         dict_get(io->message, "property.cmd", "unknown") :
         !strcmp(family, "media") ? dict_get(io->message, "media.cmd", "unknown") :
         !strcmp(family, "talk") ? dict_get(io->message, "talk.cmd", "unknown") :
         !strcmp(family, "file") ? dict_get(io->message, "file.cmd", "unknown") : "unknown";
      ws_send_error(client, "Cannot relay %.32s.%.32s to %.64s: %s",
         family, command, c->owner,
         !client->authenticated || !client->user ? "authentication required" :
         "operation is not supported by the relay");


      return;
   }
   struct relay_client *origin = rr_relay_client_for(client, true);
   dict *request = origin ? dict_clone(io->message) : NULL;

   if (request) {
      if (media && media->home) {
         dict_add(request, "media.transport", "ws");
      }
      dict_add_ulong(request, "relay.deadline", time(NULL) + 30);
      dict_add(request, "relay.codecs", client->media_codecs);

      if (media && !strcmp(dict_get(request, "media.cmd", ""), "feedback")) {
         dict_add_ulong(request, "media.stream", media->owner_stream);
      }
   }
   bool sent = request && rr_relay_packet(c->owner, "request", origin->id, client->user->name, request);
   dict_free(request);

   if (!sent) {
      ws_send_error(client, "Remote owner is unavailable");

      return;
   }
   io->result = true;
}

/* Direct-neighbor catalog synchronization accounting; bounded by the active
 * snapshot IDs and cleared when its end marker is processed. */
typedef struct {
   char *origin;
   guint expected, received, accepted, rejected;
   gint64 started;
} catalog_receive_t;
static GHashTable *catalog_receives;
static void catalog_receive_free(gpointer ptr) {
   catalog_receive_t *r = ptr;
   g_free(r->origin);
   g_free(r);
}

/* Validate a received link envelope and dispatch its payload by kind. */
static void receive(const char *event, const char *data, rrconn_t *client, void *user) {
   dict *envelope = json2dict(data);

   if (!envelope) {
      return;
   }
   const char *origin = dict_get(envelope, "node", ""), *dest = dict_get(envelope, "dest", ""),
      *kind = dict_get(envelope, "kind", ""), *id = dict_get(envelope, "session", "");
   dict *d = json2dict(dict_get(envelope, "payload", ""));

   if (!d) {
      goto done;
   }

   if (!strcmp(kind, "catalog-begin") && rr_relay_local_owner(dest)) {
      if (*id && rrserver_links_route(origin) && !strcmp(rrserver_links_route(origin), origin)) {
         if (!catalog_receives)
            catalog_receives = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, catalog_receive_free);
         GHashTableIter iter;
         gpointer key, value;
         gint64 now = g_get_monotonic_time();
         g_hash_table_iter_init(&iter, catalog_receives);
         while (g_hash_table_iter_next(&iter, &key, &value)) {
            catalog_receive_t *old = value;
            if (now - old->started > 60 * G_USEC_PER_SEC) {
               Log(LOG_WARN, "relay.catalog", "Discarding incomplete incoming sync %s from %s (%u/%u received)",
                   (char *)key, old->origin, old->received, old->expected);
               g_hash_table_iter_remove(&iter);
            }
         }
         catalog_receive_t *r = g_new0(catalog_receive_t, 1);
         r->started = now;
         r->origin = g_strdup(origin);
         r->expected = (guint)dict_get_ulong(d, "catalog.expected", 0);
         g_hash_table_replace(catalog_receives, g_strdup(id), r);
      }
      goto done;
   }
   if (!strcmp(kind, "catalog-end") && rr_relay_local_owner(dest)) {
      catalog_receive_t *r = catalog_receives ? g_hash_table_lookup(catalog_receives, id) : NULL;
      if (r && !strcmp(r->origin, origin) && rrserver_links_route(origin) &&
          !strcmp(rrserver_links_route(origin), origin)) {
         dict *ack = dict_new();
         if (ack) {
            dict_add(ack, "op", "link.application");
            dict_add(ack, "node", rrserver_links_node());
            dict_add(ack, "dest", origin);
            dict_add(ack, "kind", "catalog-ack");
            dict_add(ack, "session", id);
            dict_add(ack, "username", "");
            dict *stats = dict_new();
            dict_add_ulong(stats, "catalog.received", r->received);
            dict_add_ulong(stats, "catalog.accepted", r->accepted);
            dict_add_ulong(stats, "catalog.rejected", r->rejected);
            dict_add_ulong(stats, "catalog.expected", r->expected);
            if (r->received != r->expected)
               Log(LOG_WARN, "relay.catalog", "Incoming sync %s from %s incomplete: expected=%u received=%u",
                   id, origin, r->expected, r->received);
            char *json = dict2json(stats);
            dict_free(stats);
            dict_add(ack, "payload", json ? json : "{}");
            free(json);
            rrserver_links_send(origin, ack);
            dict_free(ack);
         }
         g_hash_table_remove(catalog_receives, id);
      }
      goto done;
   }
   if (!strcmp(kind, "catalog-ack") && rr_relay_local_owner(dest)) {
      if (rrserver_links_route(origin) && !strcmp(rrserver_links_route(origin), origin))
         rr_relay_catalog_ack(origin, id,
             (guint)dict_get_ulong(d, "catalog.received", 0),
             (guint)dict_get_ulong(d, "catalog.accepted", 0),
             (guint)dict_get_ulong(d, "catalog.rejected", 0));
      goto done;
   }

   if (!strcmp(kind, "request")) {
      time_t deadline = dict_get_time_t(d, "relay.deadline", 0);

      /* Compare actual wall clock time, not the event-loop cached 'now'. Allow a small clock-skew margin between linked hosts. */
      time_t received_at = time(NULL);

      if (!deadline || deadline < received_at - 5 || deadline > received_at + 35) {
         dict *error = dict_new();

         if (error) {
            const char *family = dict_get(d, "msg.type", "unknown");
            const char *operation = !strcmp(family, "talk") ? dict_get(d, "talk.cmd", "unknown") :
               !strcmp(family, "file") ? dict_get(d, "file.cmd", "unknown") :
               !strcmp(family, "object") ? dict_get(d, "object.cmd", "unknown") :
               !strcmp(family, "media") ? dict_get(d, "media.cmd", "unknown") : "request";
            const char *target = !strcmp(family, "talk") ? dict_get(d, "talk.target", "") :
               !strcmp(family, "file") ? dict_get(d, "file.path", "") : "";
            /* Client-facing failure identifies the action and destination;
             * no request body, passwords or authentication tokens. */
            char reason[512];
            snprintf(reason, sizeof(reason),
               "Remote %.24s.%.24s%s%.160s to node %.40s rejected: %s (deadline=%lld, receiver=%lld; check linked-server clocks and relay latency)",
               family, operation, *target ? " for " : "", *target ? target : "", dest,
               !deadline ? "missing request deadline" :
               deadline < received_at - 5 ? "request deadline passed" : "request deadline too far in the future",
               (long long) deadline, (long long) received_at);
            Log(LOG_WARN, "relay", "%s; origin %.40s", reason, origin);
            dict_add(error, "msg.type", "error");
            dict_add(error, "error.msg", reason);
            rr_relay_packet(origin, "reply", id, NULL, error);
            dict_free(error);
         }
         goto done;
      }
   }

   if (!strcmp(kind, "subscribe") || !strcmp(kind, "unsubscribe")) {
      const char *room = dict_get(d, "room", "");
      struct catalog *c = rr_relay_room_catalog(room);
      const char *from = rrserver_links_route(origin), *toward = rrserver_links_route(dest);

      if (!c || strcmp(c->owner, dest) || !from || strcmp(from, origin) || (toward && !strcmp(from, toward))) {
         goto done;
      }
      bool before = rr_relay_demand(dest, room);
      struct subscription **p = &rr_relay_subscriptions;
      while (*p && (strcmp((*p)->owner, dest) || strcmp((*p)->room, room) || strcmp((*p)->node, origin))) {
         p = &(*p)->next;
      }

      if (!strcmp(kind, "subscribe") && !*p && rr_relay_subscription_count < MAX_SUBSCRIPTIONS) {
         struct subscription *s = calloc(1, sizeof(*s));

         if (s) {
            strcpy(s->owner, dest);
            snprintf(s->room, sizeof(s->room), "%s", room);
            strcpy(s->node, origin);
            s->next = rr_relay_subscriptions;
            rr_relay_subscriptions = s;
            rr_relay_subscription_count++;
         }
      } else if (!strcmp(kind, "unsubscribe") && *p) {
         struct subscription *s = *p;
         *p = s->next;
         free(s);
         rr_relay_subscription_count--;
         Log(LOG_DEBUG, "relay", "Subscription removed owner %s room %s branch %s", dest, room, origin);
      }
      bool after = rr_relay_demand(dest, room);

      if (before != after) {
         rr_relay_subscribe_upstream(dest, room, after);
      }
   } else if (!rr_relay_local_owner(dest)) {
      rrserver_links_send(dest, envelope);
   } else if (!strcmp(kind, "catalog") || !strcmp(kind, "catalog-remove") || !strcmp(kind, "users")) {
      if (!strcmp(kind, "catalog")) {
         catalog_receive_t *sync = (*id && catalog_receives) ?
            g_hash_table_lookup(catalog_receives, id) : NULL;
         if (sync) sync->received++;
         rr_relay_catalog_put(origin, d);
         if (sync) {
            const char *key = dict_get(d, "catalog.service", NULL);
            if (!key) key = dict_get(d, "object.uuid", NULL);
            char roomkey[160];
            if (!key) {
               snprintf(roomkey, sizeof(roomkey), "room:%s", dict_get(d, "catalog.room", ""));
               key = roomkey;
            }
            struct catalog *stored = rr_relay_find_catalog(key);
            char *received_json = dict2json(d);
            char *stored_json = stored && !strcmp(stored->owner, origin) ?
               dict2json(stored->descriptor) : NULL;
            gboolean accepted = received_json && stored_json && !strcmp(received_json, stored_json);
            if (accepted) sync->accepted++;
            else sync->rejected++;
            free(received_json);
            free(stored_json);
         }

         if (dict_get(d, "object.uuid", NULL)) {
            dict *added = dict_clone(d);

            if (added) {
               dict_add(added, "object.cmd", "added");
               rrserver_objects_remote(NULL, added);
               dict_free(added);
            }
         }
      } else if (!strcmp(kind, "users")) {
         rr_relay_directory_put(origin, d);
      } else {
         const char *uuid = dict_get(d, "object.uuid", NULL), *room = dict_get(d, "catalog.room", NULL);
         const char *service = dict_get(d, "catalog.service", NULL);
         char key[160];
         snprintf(key, sizeof(key), "%s%s", service || uuid ? "" : "room:", service ? service : uuid ? uuid : room ? room : "");
         struct catalog **p = &rr_relay_catalogs;
         while (*p) {
            struct catalog *entry = *p;

            if ((!strcmp(entry->key, key) || (!uuid && room && !strcasecmp(entry->key, key))) && !strcmp(entry->owner, origin)) {
               if (uuid) {
                  dict_add(d, "msg.type", "object");
                  dict_add(d, "object.cmd", "removed");
                  rrserver_objects_remote(NULL, d);
               }
               *p = entry->next;
               dict_free(entry->descriptor);
               free(entry);
               rr_relay_catalog_count--;
               break;
            } else {
               p = &entry->next;
            }
         }
      }

      // Each receiving branch forwards discovery to the other direct branches.
      for (struct catalog *c = rr_relay_catalogs ; c ; c = c->next) {
         if (strcmp(dict_get(c->descriptor, "object.type", ""), "node")) {
            continue;
         }
         const char *next = rrserver_links_route(c->owner), *from = rrserver_links_route(origin);

         if (next && !strcmp(next, c->owner) && from && strcmp(next, from)) {
            dict_add(envelope, "dest", next);
            rrserver_links_send(next, envelope);
         }
      }
   } else if (!strcmp(kind, "request")) {
      rr_relay_execute(origin, id, dict_get(envelope, "username", ""), d);
   } else if (!strcmp(kind, "reply")) {
      for (struct relay_client *c = rr_relay_clients ; c ; c = c->next) {
         if (!strcmp(c->id, id)) {
            const char *cmd = dict_get(d, "talk.cmd", ""), *room = dict_get(d, "talk.room", NULL);

            if (room && (!strcmp(cmd, "join") || !strcmp(cmd, "part")) && !strcmp(dict_get(d, "talk.session", ""), id)) {
               struct catalog *authority = rr_relay_room_catalog(room);

               if (!authority || strcmp(authority->owner, origin)) {
                  break;
               }
               rr_relay_wanted_set(c, origin, room, !strcmp(cmd, "join"));
               bool before = rr_relay_demand(origin, room);

               if (!strcmp(cmd, "part")) {
                  rr_relay_media_forget(c->client, origin, room);
               }
               rr_relay_update_membership(c->client, room, !strcmp(cmd, "join"));
               bool after = rr_relay_demand(origin, room);

               if (before != after) {
                  rr_relay_subscribe_upstream(origin, room, after);
               }
               dict_add(d, "talk.session", c->client->token);
            }

            if (!strcmp(dict_get(d, "msg.type", ""), "media")) {
               const char *media_cmd = dict_get(d, "media.cmd", "");
               struct relay_media *m = rr_relay_media_map(c->client, dict_get(d, "media.chan-uuid", ""), !strcmp(media_cmd, "available"));

               if (!m) {
                  break;
               }

               if (!strcmp(media_cmd, "available")) {
                  const char *room = dict_get(d, "media.room", "");
                  struct catalog *authority = rr_relay_room_catalog(room);

                  if (!authority || strcmp(authority->owner, origin) || !ws_client_in_room(c->client, room)) {
                     break;
                  }
                  snprintf(m->owner, sizeof(m->owner), "%s", origin);
                  snprintf(m->room, sizeof(m->room), "%s", room);
                  m->direction = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_NA);
                  m->subsystem = dict_get_ulong(d, "media.subsys", 0);
               } else if (strcmp(m->owner, origin) || !ws_client_in_room(c->client, m->room)) {
                  break;
               }

               if (!strcmp(media_cmd, "subscribed")) {
                  m->owner_stream = dict_get_ulong(d, "media.stream", 0);
                  m->subscribed = true;
                  dict_add_ulong(d, "media.owner-stream", m->owner_stream);
                  dict_add_ulong(d, "media.stream", m->stream);

                  if (m->home) {
                     dict_del(d, "media.direct");
                     dict_del(d, "media.direct-key");
                     dict_del(d, "media.direct-ssrc");
                     dict_del(d, "media.direct-security");
                     dict_add(d, "media.transport", "ws");
                     rrserver_direct_relay_grant(c->client, d, m->stream, m->direction);
                  }
               } else if (!strcmp(media_cmd, "unsubscribed") || !strcmp(media_cmd, "chan-remove")) {
                  m->subscribed = false;
               } else if (!strcmp(media_cmd, "frame")) {
                  if (m->subscribed && rr_relay_media_reassemble(m, d)) {
                     struct rr_binframe frame;

                     if (!rr_binframe_parse(m->frame, m->size, &frame) && frame.hdr.stream == m->owner_stream) {
                        m->frame[11] = m->stream;
                        struct mg_str bytes = {
                           .buf = (char *)m->frame, .len = m->size
                        };

                        if (m->setup) {
                           ws_send_setup_to_cptr(NULL, c->client, &bytes);
                        } else {
                           ws_send_to_cptr(NULL, c->client, &bytes, WEBSOCKET_OP_BINARY);
                        }
                     }
                     free(m->frame);
                     m->frame = NULL;
                  }
                  break;
               }
            }

            if (!strcmp(dict_get(d, "msg.type", ""), "file")) {
               const char *original = dict_get(d, "file.path", "/");
               char virtual_path[1100];
               snprintf(virtual_path, sizeof(virtual_path), "/@%s%s", origin, original);
               dict_add(d, "file.path", virtual_path);
            }
            rr_relay_deliver(c->client, d);
            break;
         }
      }
   } else if (!strcmp(kind, "state")) {
      const char *room = rr_relay_message_room(d);
      struct catalog *c = rr_relay_room_catalog(room);
      // State may come from an intermediate subscribed neighbor; its catalog
      // still identifies the only authority for this room.
      const char *authority = dict_get(envelope, "username", ""), *from = rrserver_links_route(origin);
      const char *toward = rrserver_links_route(authority);

      if (!c || !room || strcmp(c->owner, authority) || !from || !toward || strcmp(from, toward) || !rr_relay_state_demand(c->owner, room, d)) {
         goto done;
      }

      for (struct relay_client *local = rr_relay_clients ; local ; local = local->next) {
         if (rr_relay_client_interested(local->client, room, d)) {
            rr_relay_deliver(local->client, d);
         }
      }

      rr_relay_publish_state(c->owner, room, d);
   } else if (!strcmp(kind, "close")) {
      struct remote_session **p = &rr_relay_remotes;
      while (*p) {
         struct remote_session *r = *p;

         if (!strcmp(r->origin, origin) && !strcmp(r->id, id)) {
            if (!rr_relay_remove_remote(p)) {
               p = &r->next;
            }
         } else {
            p = &r->next;
         }
      }
   }
done:
   dict_free(d);
   dict_free(envelope);
}

/* Send current discovery and users when a direct server link becomes ready. */
static void ready(const char *event, const char *node, rrconn_t *client, void *user) {
   rr_relay_collect_local();
   rr_relay_users_poll(true);
   rr_relay_send_catalogs(node);
   rrserver_relay_poll();

   for (struct directory *entry = rr_relay_directories ; entry ; entry = entry->next) {
      const char *route = rrserver_links_route(entry->node);

      if (!route || strcmp(route, node)) {
         rr_relay_send_owned(node, entry->node, "users", entry->users);
      }
   }
}

/* Release relay subscriptions and notify owners when a local session closes. */
static void close_client(const char *event, const char *data, rrconn_t *client, void *user) {
   rr_relay_media_forget(client, NULL, NULL);
   struct relay_client **p = &rr_relay_clients;
   while (*p) {
      struct relay_client *c = *p;

      if (c->client != client) {
         p = &c->next;
         continue;
      }
      char rooms[AUTOJOIN_LEN];
      snprintf(rooms, sizeof(rooms), "%s", client->rooms);
      *p = c->next;
      rr_relay_client_count--;

      for (struct catalog *entry = rr_relay_catalogs ; entry ; entry = entry->next) {
         if (!strcmp(dict_get(entry->descriptor, "object.type", ""), "node") && !rr_relay_local_owner(entry->owner)) {
            dict *d = dict_new();

            if (d) {
               rr_relay_packet(entry->owner, "close", c->id, NULL, d);
               dict_free(d);
            }
         }
      }

      char *save = NULL;

      for (char *room = strtok_r(rooms, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
         struct catalog *entry = rr_relay_room_catalog(room);

         if (entry && !rr_relay_demand(entry->owner, room)) {
            rr_relay_subscribe_upstream(entry->owner, room, false);
         }
      }

      rr_relay_wanted_free(c);
      free(c);
   }
}

/* Remove discovery and session state made unreachable by a lost link. */
static void lost(const char *event, const char *node, rrconn_t *client, void *user) {
   for (struct relay_client *local = rr_relay_clients ; local ; local = local->next) {
      for (struct wanted_room *w = local->wanted ; w ; w = w->next) {
         if (!rrserver_links_route(w->owner)) {
            w->pending = true;
         }
      }
   }

   struct relay_media *m = rr_relay_media_maps;
   while (m) {
      struct relay_media *next = m->next;

      if (*m->owner && !rrserver_links_route(m->owner)) {
         char owner[37];
         snprintf(owner, sizeof(owner), "%s", m->owner);
         rr_relay_media_forget(NULL, owner, NULL);
         m = rr_relay_media_maps;
      } else {
         m = next;
      }
   }
   struct subscription **sp = &rr_relay_subscriptions;
   while (*sp) {
      struct subscription *s = *sp;

      if ((!rr_relay_local_owner(s->owner) && !rrserver_links_route(s->owner)) || !rrserver_links_route(s->node)) {
         *sp = s->next;
         free(s);
         rr_relay_subscription_count--;
      } else {
         sp = &s->next;
      }
   }
   struct remote_session **rp = &rr_relay_remotes;
   while (*rp) {
      struct remote_session *r = *rp;

      if (!rrserver_links_route(r->origin)) {
         if (!rr_relay_remove_remote(rp)) {
            rp = &r->next;
         }
      } else {
         rp = &r->next;
      }
   }
   struct directory **dp = &rr_relay_directories;
   while (*dp) {
      struct directory *entry = *dp;

      if (!rr_relay_local_owner(entry->node) && !rrserver_links_route(entry->node)) {
         *dp = entry->next;
         dict_free(entry->users);
         free(entry);
      } else {
         dp = &entry->next;
      }
   }
   struct catalog **cp = &rr_relay_catalogs;
   while (*cp) {
      struct catalog *c = *cp;

      if (rr_relay_local_owner(c->owner) || rrserver_links_route(c->owner)) {
         cp = &c->next;
         continue;
      }
      const char *room = dict_get(c->descriptor, "catalog.room", NULL);

      if (room) {
         for (struct relay_client *local = rr_relay_clients ; local ; local = local->next) {
            if (ws_client_in_room(local->client, room)) {
               dict *state = dict_new();

               if (state) {
                  dict_add(state, "msg.type", "talk");
                  dict_add(state, "talk.cmd", "room-state");
                  dict_add(state, "talk.room", room);
                  dict_add_bool(state, "talk.connected", false);
                  ws_send_dict(NULL, local->client, state, WEBSOCKET_OP_TEXT);
                  dict_free(state);
               }
               rr_relay_update_membership(local->client, room, false);
               ws_send_error(local->client, "Owner of room %s disconnected; restoring its subscription when it returns", room);
            }
         }
      }
      const char *uuid = dict_get(c->descriptor, "object.uuid", NULL);

      if (uuid) {
         dict *removed = dict_new();

         if (removed) {
            dict_add(removed, "msg.type", "object");
            dict_add(removed, "object.cmd", "removed");
            dict_add(removed, "object.uuid", uuid);
            rrserver_objects_remote(NULL, removed);
            dict_free(removed);
         }
      }
      *cp = c->next;
      dict_free(c->descriptor);
      free(c);
      rr_relay_catalog_count--;
   }
}

/* A revoked grant is gone even if the backend rejected its first key-up. Keep retrying from the owning session, independently of grant lifetime. */
static void retry_remote_ptt(struct remote_session *r, uint64_t at) {
   if (!r->client.is_ptt || rrserver_direct_tx_ready(&r->client, r->client.ptt_room) || rrserver_relay_proxy_tx_active(&r->client, r->client.ptt_room, at)) {
      r->release_retry = 0;

      return;
   }

   if (at < r->release_retry) {
      return;
   }
   ws_release_ptt_on_disconnect(&r->client);
   r->release_retry = r->client.is_ptt ? at + UINT64_C(1000000) : 0;
}

/* Expire media fragments, retry sessions and reconcile catalogs/user directories. */
extern void rr_relay_catalog_pump(void);
void rrserver_relay_poll(void) {
   rr_relay_catalog_pump();
   for (struct relay_media *m = rr_relay_media_maps ; m ; m = m->next) {
      if (m->frame && (uint64_t) mono_us() - m->started > (m->setup ? 1000000 : 100000)) {
         free(m->frame);
         m->frame = NULL;
      }
   }

   struct remote_session **pending = &rr_relay_remotes;
   while (*pending) {
      struct remote_session *r = *pending;

      if (!r->client.authenticated || !r->client.user || !r->client.user->enabled ||
         r->client.user->password_change_required || (r->client.user->password_expires && r->client.user->password_expires <= now)) {
         if (!rr_relay_remove_remote(pending)) {
            pending = &r->next;
         }
      } else {
         retry_remote_ptt(r, mono_us());
         pending = &r->next;
      }
   }

   if (!rr_relay_initialized && *rrserver_links_node()) {
      rr_relay_collect_local();
      rr_relay_initialized = true;
   }

   if (!*rrserver_links_node()) {
      return;
   }

   static long long catalog_at;

   if (rr_relay_catalog_dirty || mono_ms() - catalog_at >= 30000) {
      catalog_at = mono_ms();
      rr_relay_refresh_catalog();
   }

   for (struct relay_client *client = rr_relay_clients ; client ; client = client->next) {
      if (!client->client->authenticated || !client->client->user || client->retry_at > mono_ms()) {
         continue;
      }

      for (struct wanted_room *w = client->wanted ; w ; w = w->next) {
         if (!w->pending) {
            continue;
         }
         struct catalog *c = rr_relay_room_catalog(w->room);

         if (!w->pending || !c || strcmp(c->owner, w->owner) || !rrserver_links_route(w->owner)) {
            continue;
         }
         dict *join = dict_new();

         if (join) {
            dict_add(join, "msg.type", "talk");
            dict_add(join, "talk.cmd", "join");
            dict_add(join, "talk.target", w->room);
            dict_add_ulong(join, "relay.deadline", now + 10);

            if (rr_relay_packet(w->owner, "request", client->id, client->client->user->name, join)) {
               w->pending = false;
            }
            dict_free(join);
         }
         client->retry_at = mono_ms() + 1000;
      }
   }

   rr_relay_users_poll(false);
}

/* Register relay listeners once on the server event loop. */
void rrserver_relay_register_events(void) {
   rr_relay_users_register();
   event_on("room.add", rr_relay_dirty, NULL);
   event_on("room.delete", rr_relay_dirty, NULL);
   event_on("room.vfo", rr_relay_dirty, NULL);
   event_on("object.model.added", rr_relay_dirty, NULL);
   event_on("object.model.removed", rr_relay_dirty, NULL);
   event_on("object.model.schema", rr_relay_dirty, NULL);
   event_on("server.link.application", receive, NULL);
   event_on("server.link.ready", ready, NULL);
   event_on("server.link.lost", lost, NULL);
   event_on("serial.session.closed", close_client, NULL);
   event_on("protocol.object.close", close_client, NULL);
   event_on(RR_SERVER_ROSTER_EVENT, rr_relay_roster, NULL);
   event_on_binary(RR_MEDIA_CATALOG_EVENT, rr_relay_media_catalog, NULL);
   event_on_binary(RR_SERVER_TALKER_EVENT, rr_relay_remote_talker, NULL);
   event_on_binary(RR_MEDIA_BINARY_EVENT, rr_relay_media_outgoing, NULL);
   event_on_binary(RR_MEDIA_INBOUND_EVENT, rr_relay_media_incoming, NULL);
   event_on_binary("media.frame.tx.channel", rr_relay_proxy_tx_frame, NULL);
   event_on_binary(RR_MEDIA_FANOUT_EVENT, rr_relay_media_fanout, NULL);
   event_on_binary(RR_MEDIA_DEMAND_EVENT, rr_relay_media_demand, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, route_request, NULL);
   event_on_binary(RR_SERVER_SEND_EVENT, rr_relay_send_remote, NULL);
   event_on_binary(RR_SERVER_PUBLISH_EVENT, rr_relay_publish, NULL);
}

/* Release relay-owned catalogs, subscriptions and sessions during shutdown. */
void rrserver_relay_fini(void) {
   rr_relay_media_forget(NULL, NULL, NULL);
   while (rr_relay_directories) {
      struct directory *entry = rr_relay_directories;
      rr_relay_directories = entry->next;
      dict_free(entry->users);
      free(entry);
   }
   while (rr_relay_catalogs) {
      struct catalog *c = rr_relay_catalogs;
      rr_relay_catalogs = c->next;
      dict_free(c->descriptor);
      free(c);
   }
   while (rr_relay_clients) {
      struct relay_client *c = rr_relay_clients;
      rr_relay_clients = c->next;
      rr_relay_wanted_free(c);
      free(c);
   }
   while (rr_relay_remotes && rr_relay_remove_remote(&rr_relay_remotes)) {
   }
   while (rr_relay_subscriptions) {
      struct subscription *s = rr_relay_subscriptions;
      rr_relay_subscriptions = s->next;
      free(s);
   }
   rr_relay_catalog_count = rr_relay_client_count = rr_relay_remote_count = rr_relay_subscription_count = rr_relay_wanted_count = 0;
   rr_relay_initialized = false;
   rr_relay_users_reset();
}

#else
/* Check accepted-audio liveness and remaining proxy TX authority. */
bool rrserver_relay_proxy_tx_active(rrconn_t *client, const char *room, uint64_t at) {
   return false;
}

/* Check a subscribed home-side audio stream against its session and room. */
bool rrserver_relay_media_valid(rrconn_t *client, const char *uuid, unsigned stream) {
   return false;
}

/* Register relay listeners once on the server event loop. */
void rrserver_relay_register_events(void) {
}

/* Expire media fragments, retry sessions and reconcile catalogs/user directories. */
void rrserver_relay_poll(void) {
}

/* Release relay-owned catalogs, subscriptions and sessions during shutdown. */
void rrserver_relay_fini(void) {
}

/* Send remote inventory entries visible through the relay catalog. */
void rrserver_relay_inventory(rrconn_t *client, const char *request) {
}
void rrserver_relay_file_nodes(rrconn_t *client, const char *request_id) {
   (void)client;
   (void)request_id;
}

/* Send discovered remote objects to an object-list requester. */
void rrserver_relay_objects(rrconn_t *client, const char *request) {
}

/* Return an owned listing of rooms advertised by remote nodes. */
char *rrserver_relay_remote_rooms(void) {
   return strdup("");
}

/* Return the borrowed home-server alias, falling back to its UUID. */
const char *rrserver_relay_client_server(const rrconn_t *client) {
   (void)client;

   return cfg_get("station.name");
}

/* Return an owned listing of discovered server nodes. */
char *rrserver_relay_servers(void) {
   return strdup("");
}

/* Return an owned listing combining local and remote room names. */
char *rrserver_relay_rooms(const char *local) {
   return strdup(local ? local : "");
}
#endif

/* Visit borrowed remote sessions synchronously on the server thread. */
void rrserver_relay_visit_sessions(void (*visit) (rrconn_t *, void *), void *data) {
#ifdef USE_MONGOOSE

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      visit(&r->client, data);
   }

#else
   (void)visit;
   (void)data;
#endif
}

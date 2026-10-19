//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Virtual owner sessions, room demand and authoritative request execution. */
#include "build_config.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/media.health.h>
#ifdef USE_MONGOOSE
#include <librrprotocol/server.events.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/wire.h>
#include <rrserver/supervision.h>
#include <rrserver/relay.h>
#include <rrserver/media.direct.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <rrserver/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/rig.properties.h>
#include <rrserver/database.h>
#include <rrserver/discovery.h>
#include <rrserver/relay.internal.h>

extern struct GlobalState rig;
size_t rr_relay_wanted_count;
size_t rr_relay_remote_count;
size_t rr_relay_client_count;
struct remote_session *rr_relay_remotes;
struct relay_client *rr_relay_clients;

/* Find the borrowed remote session wrapping this protocol connection. */
struct remote_session *rr_relay_remote_for(rrconn_t *client) {
   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (&r->client == client) {
         return r;
      }
   }

   return NULL;
}

/* Find or allocate local session relay state within the session limit. */
struct relay_client *rr_relay_client_for(rrconn_t *client, bool create) {
   for (struct relay_client *c = rr_relay_clients ; c ; c = c->next) {
      if (c->client == client) {
         return c;
      }
   }

   if (!create || rr_relay_client_count == MAX_RELAY_SESSIONS) {
      return NULL;
   }
   struct relay_client *c = calloc(1, sizeof(*c));
   char *id = g_uuid_string_random();

   if (!c || !id) {
      free(c);
      g_free(id);

      return NULL;
   }
   c->client = client;
   strcpy(c->id, id);
   g_free(id);
   c->next = rr_relay_clients;
   rr_relay_clients = c;
   rr_relay_client_count++;

   return c;
}

/* Track requested remote room membership for reconnect replay. */
void rr_relay_wanted_set(struct relay_client *client, const char *owner, const char *room, bool joined) {
   struct wanted_room **link = &client->wanted;
   while (*link && strcasecmp((*link)->room, room)) {
      link = &(*link)->next;
   }

   if (joined) {
      if (!*link && rr_relay_wanted_count < MAX_SUBSCRIPTIONS) {
         *link = calloc(1, sizeof(**link));

         if (*link) {
            snprintf((*link)->room, sizeof((*link)->room), "%s", room);
            rr_relay_wanted_count++;
         }
      }

      if (*link) {
         snprintf((*link)->owner, sizeof((*link)->owner), "%s", owner);
         (*link)->pending = false;
      }
   } else if (*link) {
      struct wanted_room *wanted = *link;
      *link = wanted->next;
      free(wanted);
      rr_relay_wanted_count--;
   }
}

/* Free a local session's desired remote-room list. */
void rr_relay_wanted_free(struct relay_client *client) {
   while (client->wanted) {
      struct wanted_room *w = client->wanted;
      client->wanted = w->next;
      free(w);
      rr_relay_wanted_count--;
   }
}

/* Schedule previously requested room memberships when an owner returns. */
void rr_relay_rejoin_owned_room(const char *owner, const char *room) {
   for (rrconn_t *client = http_client_list ; client ; client = client->next) {
      if (!client->authenticated || !client->user || !ws_client_in_room(client, room)) {
         continue;
      }
      struct relay_client *local = rr_relay_client_for(client, true);
      struct wanted_room *wanted = local ? local->wanted : NULL;
      while (wanted && strcasecmp(wanted->room, room)) {
         wanted = wanted->next;
      }

      if (wanted && !strcmp(wanted->owner, owner)) {
         continue;
      }

      if (local) {
         rr_relay_wanted_set(local, owner, room, true);

         for (wanted = local->wanted ; wanted ; wanted = wanted->next) {
            if (!strcasecmp(wanted->room, room)) {
               wanted->pending = true;
               break;
            }
         }

         local->retry_at = 0;
      }
      ws_client_part_room(client, room);
      ws_send_notice(client, wanted ? "Room %s belongs to a linked server; rejoining through its owner" :
         "Room %s belongs to a linked server; use /join to join its owner", room);
   }

   /* These virtual memberships belonged to our obsolete local room. */
   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (ws_client_in_room(&r->client, room)) {
         ws_client_part_room(&r->client, room);
      }
   }

   struct subscription **link = &rr_relay_subscriptions;
   while (*link) {
      struct subscription *s = *link;

      if (!strcasecmp(s->room, room) && strcmp(s->owner, owner)) {
         *link = s->next;
         free(s);
         rr_relay_subscription_count--;
      } else {
         link = &s->next;
      }
   }
}

/* Test local sessions and downstream subscriptions for room demand. */
bool rr_relay_demand(const char *owner, const char *room) {
   for (struct relay_client *c = rr_relay_clients ; c ; c = c->next) {
      if (ws_client_in_room(c->client, room)) {
         return true;
      }
   }

   for (struct subscription *s = rr_relay_subscriptions ; s ; s = s->next) {
      if (!strcmp(s->owner, owner) && !strcmp(s->room, room)) {
         return true;
      }
   }

   return false;
}

/* Notify a room owner when aggregate branch demand changes. */
void rr_relay_subscribe_upstream(const char *owner, const char *room, bool subscribe) {
   if (rr_relay_local_owner(owner)) {
      return;
   }
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "room", room);
   rr_relay_packet(owner, subscribe ? "subscribe" : "unsubscribe", NULL, NULL, d);
   dict_free(d);
}

/* Match joined-room scope against the room addressed by a state message. */
bool rr_relay_interested(const char *joined, const char *room, dict *d) {
   const char *family = dict_get(d, "msg.type", "");

   if (strcmp(family, "cat") && strcmp(family, "property")) {
      return !strcasecmp(joined, room);
   }
   struct catalog *a = rr_relay_room_catalog(joined), *b = rr_relay_room_catalog(room);
   const char *rig_a = a ? dict_get(a->descriptor, "catalog.rig", NULL) : NULL;
   const char *rig_b = b ? dict_get(b->descriptor, "catalog.rig", NULL) : NULL;

   if (!rig_a || !rig_b || strcmp(a->owner, b->owner) || strcmp(rig_a, rig_b)) {
      return false;
   }
   uint32_t mask = (uint32_t) dict_get_ulong(a->descriptor, "catalog.mask", 0);

   if (!mask) {
      return false;
   }
   const char *vfo = dict_get(d, "cat.vfo", dict_get(d, "cat.state.vfo", NULL));

   if (!strcmp(family, "property")) {
      struct catalog *object = rr_relay_find_catalog(dict_get(d, "target", ""));

      if (object && !strcmp(dict_get(object->descriptor, "object.type", ""), "vfo")) {
         vfo = dict_get(object->descriptor, "object.alias", NULL);
      }
   }

   if (vfo && vfo[0] && !vfo[1]) {
      unsigned letter = (unsigned)toupper((unsigned char)vfo[0]);

      return letter >= 'A' && letter <= 'Z' && (mask & (UINT32_C(1) << (letter - 'A')));
   }

   return true;
}

/* Test a client's joined rooms for interest in the supplied state. */
bool rr_relay_client_interested(rrconn_t *client, const char *room, dict *d) {
   char rooms[AUTOJOIN_LEN], *save = NULL;
   snprintf(rooms, sizeof(rooms), "%s", client->rooms);

   for (char *joined = strtok_r(rooms, ",", &save) ; joined ; joined = strtok_r(NULL, ",", &save)) {
      if (rr_relay_interested(joined, room, d)) {
         return true;
      }
   }

   return false;
}

/* Test local and downstream interest before forwarding room state. */
bool rr_relay_state_demand(const char *owner, const char *room, dict *d) {
   for (struct relay_client *c = rr_relay_clients ; c ; c = c->next) {
      if (rr_relay_client_interested(c->client, room, d)) {
         return true;
      }
   }

   for (struct subscription *s = rr_relay_subscriptions ; s ; s = s->next) {
      if (!strcmp(s->owner, owner) && rr_relay_interested(s->room, room, d)) {
         return true;
      }
   }

   return false;
}

/* Forward owner state once per interested downstream branch. */
void rr_relay_publish_state(const char *owner, const char *room, dict *d) {
   for (struct subscription *s = rr_relay_subscriptions ; s ; s = s->next) {
      if (strcmp(s->owner, owner) || !rr_relay_interested(s->room, room, d)) {
         continue;
      }
      bool duplicate = false;

      for (struct subscription *before = rr_relay_subscriptions ; before != s ; before = before->next) {
         if (!strcmp(before->owner, owner) && !strcmp(before->node, s->node) && rr_relay_interested(before->room, room, d)) {
            duplicate = true;
         }
      }

      if (duplicate) {
         continue;
      }
      rr_relay_packet(s->node, "state", NULL, owner, d);
      Log(LOG_DEBUG, "relay", "State %s %s room %s to branch %s", dict_get(d, "msg.type", ""), dict_get(d, "cat.cmd", dict_get(d, "property.cmd", dict_get(d,
         "talk.cmd", ""))), room, s->node);
   }
}

/* Attach remote member information needed by local roster consumers. */
static void augment_userinfo(dict *d) {
   if (strcmp(dict_get(d, "talk.cmd", ""), "userinfo")) {
      return;
   }
   const char *name = dict_get(d, "talk.user", ""), *room = dict_get(d, "talk.room", NULL);
   unsigned count = 0;
   bool remote = false;

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (!strcasecmp(r->client.chatname, name) && ws_client_in_room(&r->client, room)) {
         count++;
         remote = true;
      }
   }

   if (!remote) {
      return;
   }

   for (rrconn_t *c = http_client_list ; c ; c = c->next) {
      if (c->authenticated && !strcasecmp(c->chatname, name) && ws_client_in_room(c, room)) {
         count++;
      }
   }

   dict_add_uint(d, "talk.sessions", count);
}

/* Refresh a remote member's room-visible state. */
static void refresh_member(const char *name, const char *room) {
   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (!strcasecmp(r->client.chatname, name) && ws_client_in_room(&r->client, room)) {
         ws_send_userinfo_room(&r->client, NULL, room);

         return;
      }
   }

   for (rrconn_t *c = http_client_list ; c ; c = c->next) {
      if (c->authenticated && !strcasecmp(c->chatname, name) && ws_client_in_room(c, room)) {
         ws_send_userinfo_room(c, NULL, room);

         return;
      }
   }
}

/* Release a remote session, retaining it if backend key-up must retry. */
bool rr_relay_remove_remote(struct remote_session **link) {
   struct remote_session *r = *link;
   char rooms[AUTOJOIN_LEN], name[HTTP_USER_LEN + 1], *save = NULL;
   snprintf(rooms, sizeof(rooms), "%s", r->client.rooms);
   snprintf(name, sizeof(name), "%s", r->client.chatname);

   if (!r->client.authenticated && r->release_retry > (uint64_t) mono_us()) {
      return false;
   }
   r->client.authenticated = false;
   event_emit("protocol.session.closed", &r->client, NULL);
   ws_release_ptt_on_disconnect(&r->client);
   rrserver_direct_close(&r->client);

   if (r->client.is_ptt) {
      r->release_retry = mono_us() + 1000000;

      return false;
   }
   rr_relay_media_forget(&r->client, NULL, NULL);
   *link = r->next;
   rr_relay_remote_count--;

   for (char *room = strtok_r(rooms, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
      dict *part = dict_new();

      if (part) {
         dict_add(part, "msg.type", "talk");
         dict_add(part, "talk.cmd", "part");
         dict_add(part, "talk.room", room);
         dict_add(part, "talk.user", name);
         ws_broadcast_room_dict(NULL, part, room);
         dict_free(part);
      }
      refresh_member(name, room);
   }

   free(r);

   return true;
}

/* Route replies emitted for an owner-side virtual session to its origin. */
void rr_relay_send_remote(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (length != sizeof(rr_server_io_t)) {
      return;
   }
   rr_server_io_t *io = (rr_server_io_t *) data;

   if (!rr_relay_applying) {
      augment_userinfo(io->message);
   }
   struct remote_session *r = rr_relay_remote_for(client);

   if (!r) {
      return;
   }
   io->handled = true;
   const char *family = dict_get(io->message, "msg.type", "");

   // Connection-wide capabilities belong to the client's home connection.
   if (!strcmp(family, "media") && (!strcmp(dict_get(io->message, "media.cmd", ""), "capab") || !strcmp(dict_get(io->message, "media.cmd", ""), "isupport"))) {
      io->result = true;

      return;
   }

   if (!strcmp(family, "media") && !strcmp(dict_get(io->message, "media.cmd", ""), "available")) {
      struct rr_mediachan *channel = media_chan_find_uuid(dict_get(io->message, "media.chan-uuid", ""));

      if (!channel || !media_client_in_channel_room(client, channel)) {
         io->result = true;

         return;
      }
   }
   dict *forward = io->message;

   if (!strcmp(dict_get(forward, "talk.session", ""), r->client.token)) {
      forward = dict_clone(io->message);

      if (!forward) {
         return;
      }
      // The existing authentication token field is shorter than a UUID.
      // Carry the complete relay session ID; the origin substitutes its token.
      dict_add(forward, "talk.session", r->id);
   }
   io->result = rr_relay_packet(r->origin, "reply", r->id, NULL, forward);

   if (forward != io->message) {
      dict_free(forward);
   }
}

/* Relay locally published state to interested branches. */
void rr_relay_publish(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (rr_relay_applying || length != sizeof(rr_server_publish_t)) {
      return;
   }
   const rr_server_publish_t *p = data;
   augment_userinfo(p->message);
   const char *room = p->room ? p->room : rr_relay_message_room(p->message);
   dict *message = p->message;

   if (!room && !strcmp(dict_get(message, "msg.type", ""), "cat") && dict_get(message, "cat.state.vfo", NULL)) {
      // Legacy cat.state is emitted only by the default-rig adapter. Give
      // its link copy an explicit scope; keep the local wire path intact.
      room = ws_authoritative_room();
      message = dict_clone(p->message);

      if (!message) {
         return;
      }
      dict_add(message, "cat.room", room);
   }
   struct catalog *c = rr_relay_room_catalog(room);

   if (c && rr_relay_local_owner(c->owner)) {
      rr_relay_publish_state(c->owner, room, message);
   }

   if (message != p->message) {
      dict_free(message);
   }
}

/* Answer roster requests with merged relay user information. */
void rr_relay_roster(const char *event, const char *room, rrconn_t *client, void *user) {
   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (ws_client_in_room(&r->client, room)) {
         ws_send_userinfo_room(&r->client, client, room);
      }
   }
}

/* Find or authenticate an owner-side virtual session for the originating user. */
static struct remote_session *owner_session(const char *origin, const char *id, const char *username) {
   /* The relay envelope is authenticated by the linked server transport.
    * A remote principal is NOT a local login and must not inherit a local
    * user's uid, password, system roles, or permissions. */
   if (!origin || !rr_object_uuid_valid(origin) || !rrserver_links_route(origin) ||
       !rr_object_uuid_valid(id) || !username || !*username ||
       strlen(username) > HTTP_USER_LEN) return NULL;

   for (struct remote_session *r = rr_relay_remotes; r; r = r->next) {
      if (strcmp(r->origin, origin) || strcmp(r->id, id)) continue;
      if (!r->client.authenticated || strcasecmp(r->client.chatname, username))
         return NULL;
      return r;
   }
   if (rr_relay_remote_count == MAX_RELAY_SESSIONS) return NULL;
   struct remote_session *r = calloc(1, sizeof(*r));
   if (!r) return NULL;
   strcpy(r->origin, origin);
   strcpy(r->id, id);
   r->remote_account.uid = -1; /* has_priv() must never match local roles */
   r->remote_account.enabled = true;
   snprintf(r->remote_account.name, sizeof(r->remote_account.name), "%s", username);
   r->client.authenticated = r->client.active = r->client.is_ws = true;
   r->client.user = &r->remote_account;
   snprintf(r->client.chatname, sizeof(r->client.chatname), "%s", username);
   snprintf(r->client.token, sizeof(r->client.token), "%s", id);
   r->next = rr_relay_remotes;
   rr_relay_remotes = r;
   rr_relay_remote_count++;
   return r;
}

/* Apply a relayed request through authoritative owner-side admission checks. */
void rr_relay_execute(const char *origin, const char *id, const char *username, dict *d) {
   struct remote_session *r = owner_session(origin, id, username);
   const char *room = rr_relay_message_room(d);
   struct catalog *c = rr_relay_room_catalog(room);

   bool files = !strcmp(dict_get(d, "msg.type", ""), "file");
   /* Authentication of the originating link is not an authorization grant.
    * Remote subjects need explicit resource grants on the owning server;
    * they must never be mapped to a local login merely to get a UID. */
   bool scoped = false;
#ifdef USE_SQLITE
   if (r && !files && room && *room) {
      const char *host = rrserver_relay_client_server(&r->client);
      rr_server_rig_t *radio = rrserver_rig_for_room(room);
      const char *kind = radio ? "rig" : "room";
      const char *uuid = radio ? rr_server_rig_id(radio) : NULL;
      char *owned_uuid = radio ? NULL : db_room_uuid(masterdb, room);
      if (!radio) uuid = owned_uuid;
      if (host && *host && uuid && *uuid && strcmp(host, cfg_get("station.name"))) {
         scoped = db_resource_access(masterdb, username, host, kind, uuid,
            radio ? "rig.use" : "room.join", time(NULL)) == RR_RESOURCE_ALLOW;
      }
      free(owned_uuid);
   }
#endif
   if (!r || !scoped || (!files && (!c || !rr_relay_local_owner(c->owner))) ||
       !rr_relay_permitted_message(d)) {
      dict *error = dict_new();

      if (error && files) {
         dict_add(error, "msg.type", "file");
         dict_add(error, "file.cmd", "result");
         dict_add(error, "request.id", dict_get(d, "request.id", ""));
         dict_add(error, "file.path", dict_get(d, "file.path", "/"));
         dict_add(error, "file.error", "Remote share access requires an explicit resource grant");
      } else if (error) {
         dict_add(error, "msg.type", "error");
         dict_add(error, "error.msg", "Remote operation requires an authorized resource grant");
         rr_relay_packet(origin, "reply", id, NULL, error);
         dict_free(error);
      }

      return;
   }
   const char *source_ip = dict_get(d, "relay.ip", NULL);
   acl_subject_t source;

   if (source_ip) {
      if (!acl_subject_init(&source, source_ip, NULL, NULL, NULL) || (*r->client.user_ip && strcmp(r->client.user_ip, source_ip))) {
         ws_send_error(&r->client, "Invalid remote session source address");

         return;
      }
      snprintf(r->client.user_ip, sizeof(r->client.user_ip), "%s", source_ip);
   }
   dict_del(d, "relay.ip");
   const char *codecs = dict_get(d, "relay.codecs", NULL);

   if (codecs) {
      snprintf(r->client.media_codecs, sizeof(r->client.media_codecs), "%s", codecs);
   }
   dict_del(d, "relay.codecs");
   dict_del(d, "relay.deadline");
   bool ptt_on = (!strcmp(dict_get(d, "cat.cmd", ""), "ptt") && dict_get_bool(d, "cat.ptt", false)) ||
      (!strcmp(dict_get(d, "property.name", ""), "ptt") && dict_get_bool(d, "property.value", false));

   if (ptt_on && !rrserver_direct_tx_ready(&r->client, room) && !rr_relay_proxy_tx_subscribed(r, room)) {
      ws_send_error(&r->client, "Remote PTT requires a live direct TX attachment or a proxy TX subscription");

      return;
   }

   if (!strcmp(dict_get(d, "msg.type", ""), "media") && !strcmp(dict_get(d, "media.cmd", ""), "frame")) {
      struct rr_mediachan *channel = media_chan_find_uuid(dict_get(d, "media.chan-uuid", ""));

      if (!channel || strcmp(channel->room, room) || !media_client_in_channel_room(&r->client, channel)) {
         return;
      }

      if (channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO && (channel->direction != RR_BINFRAME_DIR_TX ||
         (!r->proxy_tx[channel - media_channels] && !rrserver_direct_tx_ready(&r->client, room)) ||
         !chan_id_in_array(r->client.tx_channels, MAX_TX_CHANNELS, channel - media_channels + 1))) {
         return;
      }
      struct relay_media *m = rr_relay_media_map(&r->client, channel->uuid, true);

      if (m && rr_relay_media_reassemble(m, d)) {
         struct rr_binframe frame;

         if (!rr_binframe_parse(m->frame, m->size, &frame) && frame.hdr.stream == channel - media_channels + 1) {
            ws_binframe_process_mg(&r->client, (const char *)m->frame, m->size);
         }
         free(m->frame);
         m->frame = NULL;
      }

      return;
   }
   /* File requests are roomless. The owner-side file handler authenticates
    * this remote session and checks the local account's share privileges. */
   char *wire = rr_wire_encode(d);

   if (!wire) {
      ws_send_error(&r->client, "Invalid remote client message");

      return;
   }
   free(wire);
   bool was_ptt = r->client.is_ptt;
   bool ok = ws_process_dict(&r->client, d);

   if (!was_ptt && r->client.is_ptt) {
      /* A bounded startup window lets the client start audio after PTT acknowledgement. */
      r->proxy_started = mono_us();
      r->proxy_frame = 0;
      r->release_retry = 0;
   }

   if (!strcmp(dict_get(d, "msg.type", ""), "media") && ok) {
      struct rr_mediachan *channel = media_chan_find_uuid(dict_get(d, "media.chan-uuid", ""));
      const char *media_cmd = dict_get(d, "media.cmd", "");

      if (channel && channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO && channel->direction == RR_BINFRAME_DIR_TX) {
         if (!strcmp(media_cmd, "subscribe")) {
            r->proxy_tx[channel - media_channels] = strcmp(dict_get(d, "media.transport", ""), "direct") != 0;
         } else if (!strcmp(media_cmd, "unsubscribe")) {
            r->proxy_tx[channel - media_channels] = false;
         }
      }
   }
   const char *cmd = dict_get(d, "talk.cmd", "");

   if (ok && !strcmp(cmd, "join") && ws_client_in_room(&r->client, room)) {
      dict *joined = dict_new();

      if (joined) {
         dict_add(joined, "msg.type", "talk");
         dict_add(joined, "talk.cmd", "join");
         dict_add(joined, "talk.room", room);
         dict_add(joined, "talk.target", room);
         dict_add(joined, "talk.user", r->client.chatname);
         dict_add(joined, "talk.session", r->id);
         dict_add_bool(joined, "room.has-vfos", ws_room_has_vfos(room));
         dict_add_bool(joined, "room.tx-control", ws_room_tx_control(room));
         dict_add_ulong(joined, "room.vfo-mask", ws_room_vfo_mask(room));
         dict_add_bool(joined, "room.rx-tunable", ws_room_rx_tunable(room));
         dict_add_ulong(joined, "room.rx-tuning-mask", ws_room_rx_tuning_mask(room));
         rr_relay_packet(origin, "reply", id, NULL, joined);
         dict_free(joined);
      }
      ws_send_userinfo_room(&r->client, NULL, room);
      rrserver_objects_room_state(&r->client, room);
      media_send_available_all(&r->client);
   } else if (ok && !strcmp(cmd, "part")) {
      refresh_member(r->client.chatname, room);
   }
}

/* Update local room membership after an authoritative remote join/part. */
void rr_relay_update_membership(rrconn_t *client, const char *room, bool joined) {
   if (joined) {
      if (!ws_client_in_room(client, room) && strlen(client->rooms) + strlen(room) + 2 < sizeof(client->rooms)) {
         if (*client->rooms) {
            strlcat(client->rooms, ",", sizeof(client->rooms));
         }
         strlcat(client->rooms, room, sizeof(client->rooms));
      }
   } else {
      char copy[AUTOJOIN_LEN], out[AUTOJOIN_LEN] = "", *save = NULL;
      snprintf(copy, sizeof(copy), "%s", client->rooms);

      for (char *r = strtok_r(copy, ",", &save) ; r ; r = strtok_r(NULL, ",", &save)) {
         if (!strcasecmp(r, room)) {
            continue;
         }

         if (*out) {
            strlcat(out, ",", sizeof(out));
         }
         strlcat(out, r, sizeof(out));
      }

      snprintf(client->rooms, sizeof(client->rooms), "%s", out);
   }
}

/* Apply a remote object update or send an ordinary reply to a local client. */
void rr_relay_deliver(rrconn_t *client, dict *d) {
   const char *family = dict_get(d, "msg.type", "");

   if (!strcmp(family, "object") || !strcmp(family, "property")) {
      rrserver_objects_remote(client, d);
   } else {
      rr_relay_applying = true;
      ws_send_dict(NULL, client, d, WEBSOCKET_OP_TEXT);
      rr_relay_applying = false;
   }
}
#endif /* USE_MONGOOSE */

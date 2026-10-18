//
// rrserver/events.rooms.c: rooms event listeners from
// librrprotocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/rig.vfo.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
#include <rrserver/ptt.h>
#include <rrserver/supervision.h>
#include <rrserver/acl.h>
#include <rrserver/bridge.access.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/media.direct.h>
#include <rrserver/relay.h>
#include <rrserver/usage.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>

extern struct GlobalState rig;

#include <rrserver/events.internal.h>

/* Persist accepted room membership metadata and return the stored topic. */
static void rrserver_handle_room_join(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
#ifdef USE_SQLITE
   const char *room = dict_get(d, "talk.room", NULL);

   if (room && !db_room_ensure(masterdb, room, dict_get_bool(d, "room.has-vfos", false), (uint32_t) dict_get_ulong(d, "room.vfo-mask", 0), cptr ? cptr->chatname
         : "server")) {
      Log(LOG_WARN, "db", "failed to persist room %s", room);
   }

   if (room && cptr) {
      char *topic = db_room_get_topic(masterdb, room);
      dict *reply = dict_new();
      dict_add(reply, "msg.type", "talk");
      dict_add(reply, "talk.cmd", "topic");
      dict_add(reply, "talk.room", room);
      dict_add(reply, "talk.topic", topic ? topic : "");
      dict_add_bool(reply, "talk.query", true);
      ws_send_dict(NULL, cptr, reply, WEBSOCKET_OP_TEXT);
      dict_free(reply);
      free(topic);
   }
#else
   (void)cptr;
#endif
   dict_free(d);
}

/* Release PTT when its operator leaves the transmitting room. */
static void rrserver_handle_room_part(const char *event, const char *room, rrconn_t *client, void *user) {
   if (client && client->is_ptt && room && rig.ptt_rig) {
      const char *base = rr_rig_registry_room(rig.rigs, rig.ptt_rig);

      if (base && !strcasecmp(base, room)) {
         rr_ptt_set_all_off_reason("left-tx-room");
      }
   }
}

/* Persist or retrieve an authorized room topic. */
static void rrserver_handle_room_topic(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", NULL);
   const char *topic = dict_get(d, "talk.topic", "");
   bool query = dict_get_bool(d, "talk.query", false);
#ifdef USE_SQLITE

   if (!room) {
      ws_send_error(cptr, "No room selected for TOPIC");
      dict_free(d);

      return;
   }

   if (query) {
      char *stored = db_room_get_topic(masterdb, room);
      dict_add(d, "talk.topic", stored ? stored : "");
      free(stored);
      ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
      dict_free(d);

      return;
   }

   if (!db_room_set_topic(masterdb, room, topic)) {
      ws_send_error(cptr, "Unable to set topic for room %s", room);
      dict_free(d);

      return;
   }
#else

   if (!query) {
      ws_send_error(cptr, "Room topics require database support");
      dict_free(d);

      return;
   }
#endif
   dict_add(d, "talk.cmd", "topic");
   dict_add(d, "talk.topic", topic ? topic : "");
   dict_add(d, "talk.user", cptr ? cptr->chatname : "");
   dict_add_bool(d, "talk.query", false);
   ws_broadcast_room_dict(NULL, d, room);
   dict_free(d);
}

/* Return configured and remote room listings to the requester. */
static void rrserver_handle_room_list(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!cptr) {
      return;
   }
   dict *reply = dict_new();
   dict_add(reply, "msg.type", "talk");
   dict_add(reply, "talk.cmd", "room-list");
   dict_add(reply, "talk.rooms", "");
#ifdef USE_SQLITE
   db_room_ensure(masterdb, ws_site_room(), false, 0, "server");
   char *rooms = db_room_list(masterdb);

   if (rooms) {
      char *all = rrserver_relay_rooms(rooms);
      dict_add(reply, "talk.rooms", all ? all : rooms);
      free(all);
      free(rooms);
   }
#endif
   char *remote = rrserver_relay_remote_rooms(), *servers = rrserver_relay_servers();

   if (remote && *remote) {
      dict_add(reply, "talk.remote-rooms", remote);
   }

   if (servers) {
      dict_add(reply, "talk.servers", servers);
   }
   free(remote);
   free(servers);
   ws_send_dict(NULL, cptr, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

/* Create or restore a room after checking station-room privileges. */
static void rrserver_handle_room_add(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!cptr || !cptr->authenticated || !cptr->user) {
      ws_send_error(cptr, "Room management requires admin or owner");

      return;
   }

   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", NULL);

   if (!ws_room_name_valid(room) || (ws_room_station_scoped(room) && !has_priv(cptr->user->uid, "admin|owner"))) {
      ws_send_error(cptr, "Station-scoped room creation requires admin or owner");
      dict_free(d);

      return;
   }

   if (ws_room_rig_base(room)) {
      ws_send_error(cptr, "Base rig rooms are server-owned");
      dict_free(d);

      return;
   }
#ifdef USE_SQLITE

   bool exists = false, deleted = false;

   if (!db_room_status(masterdb, room, &exists, &deleted)) {
      ws_send_error(cptr, "Unable to inspect room");
      dict_free(d);

      return;
   }

   if (deleted && !has_priv(cptr->user->uid, "admin|owner")) {
      ws_send_error(cptr, "Room restoration requires admin or owner");
      dict_free(d);

      return;
   }

   if (!db_room_restore(masterdb, room, cptr->chatname) || (!exists && !db_room_ensure(masterdb, room, false, 0, cptr->chatname))) {
      ws_send_error(cptr, "Unable to add room %s", room ? room : "(none)");
      dict_free(d);

      return;
   }
#else
   ws_send_error(cptr, "Room management requires database support");
   dict_free(d);

   return;
#endif
#ifdef USE_SQLITE

   if (deleted && ws_room_rig_namespace(room)) {
      rr_server_rig_t *radio = rrserver_rig_for_room(room);
      uint32_t mask = 0;
      char *bindings = db_room_vfo_list(masterdb, room), *save = NULL;

      for (char *id = bindings ? strtok_r(bindings, " \t\r\n", &save) : NULL ; id ; id = strtok_r(NULL, " \t\r\n", &save)) {
         rr_server_vfo_t *vfo = radio ? rr_server_vfo_find_uuid(radio, id) : NULL;
         rr_vfo_t index;

         if (vfo && rr_server_vfo_native_index(vfo, &index) && index >= 0 && index < 32) {
            mask |= UINT32_C(1) << index;
         }
      }

      free(bindings);
      if (ws_room_rig_namespace(room)) {
         ws_room_set_vfo_mask(room, mask);
      }
   }
#endif
   ws_send_notice(cptr, "Room %s added", room);
   dict_free(d);
}

/* List persisted VFO bindings for the requested room. */
static void rrserver_handle_room_vfo_list(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!cptr) {
      return;
   }
   dict *reply = dict_new();
   dict_add(reply, "msg.type", "talk");
   dict_add(reply, "talk.cmd", "room-vfo-list");
#ifdef USE_SQLITE
   const char *requested_room = NULL;

   if (data) {
      dict *request = json2dict(data);

      if (request) {
         requested_room = dict_get(request, "talk.room", NULL);

         if (requested_room && *requested_room) {
            dict_add(reply, "talk.room", requested_room);
         }
         char *vfos = requested_room && *requested_room ? db_room_vfo_list(masterdb, requested_room) : db_room_vfo_map_list(masterdb);
         dict_add(reply, "talk.vfos", vfos ? vfos : "");
         free(vfos);
         dict_free(request);
      } else {
         dict_add(reply, "talk.vfos", "");
      }
   } else {
      char *map = db_room_vfo_map_list(masterdb);
      dict_add(reply, "talk.vfos", map ? map : "");
      free(map);
   }
#else
   dict_add(reply, "talk.vfos", "");
#endif
   ws_send_dict(NULL, cptr, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

/* Validate and update room-to-VFO bindings. */
static void rrserver_handle_room_vfo(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!cptr || !cptr->authenticated || !cptr->user || !has_priv(cptr->user->uid, "admin|owner")) {
      ws_send_error(cptr, "Room management requires admin or owner");

      return;
   }

   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", NULL);
   const char *binding = dict_get(d, "talk.vfo", NULL);
   const char *action = dict_get(d, "talk.action", NULL);

   if (rrserver_rig_room_configured(room)) {
      ws_send_error(cptr, "Rig room VFO bindings are managed by rig configuration");
      dict_free(d);

      return;
   }
#ifdef USE_SQLITE
   rr_server_rig_t *radio = rrserver_rig_for_room(room);
   rr_server_vfo_t *vfo = binding ? rr_rig_registry_find_vfo_uuid(rig.rigs, binding) : NULL;

   if (!vfo && binding && radio) {
      const char *dot = strstr(binding, ".vfo_");

      if (dot && dot[5] && !dot[6]) {
         char alias[64];
         size_t len = (size_t)(dot - binding);

         if (len < sizeof(alias)) {
            memcpy(alias, binding, len);
            alias[len] = '\0';

            if (rr_rig_registry_find_public_alias(rig.rigs, alias) == radio) {
               char native[2] = {
                  (char)toupper((unsigned char)dot[5]), 0
               };
               vfo = rr_server_vfo_find_alias(radio, native);
            }
         }
      }
   }

   if (!vfo) {
      ws_send_error(cptr, "Unknown VFO binding");
      dict_free(d);

      return;
   }
   binding = rr_server_vfo_id(vfo);

   if (room && binding) {
      bool ok = db_room_ensure(masterdb, room, true, 0, cptr->chatname);

      if (ok && action && strcasecmp(action, "add") == 0) {
         ok = db_room_vfo_add(masterdb, room, binding);
      } else if (ok && action && strcasecmp(action, "remove") == 0) {
         ok = db_room_vfo_remove(masterdb, room, binding);
      } else {
         ok = false;
      }

      if (!ok) {
         ws_send_error(cptr, "Unable to %s VFO %s for room %s", action ? action : "update", binding ? binding : "(none)", room ? room : "(none)");
         dict_free(d);

         return;
      }
      char *vfos = db_room_vfo_list(masterdb, room);
      dict_add(d, "talk.cmd", "room-vfo");
      dict_add(d, "talk.vfos", vfos ? vfos : "");
      uint32_t mask = 0;
      char *copy = strdup(vfos ? vfos : ""), *save = NULL;

      for (char *id = copy ? strtok_r(copy, " \t\r\n", &save) : NULL ; id ; id = strtok_r(NULL, " \t\r\n", &save)) {
         rr_server_vfo_t *mapped = rr_rig_registry_find_vfo_uuid(rig.rigs, id);
         rr_vfo_t index;

         if (mapped && rr_server_vfo_native_index(mapped, &index) && index >= 0 && index < 32) {
            mask |= UINT32_C(1) << index;
         }
      }

      free(copy);
      ws_room_set_vfo_mask(room, mask);
      db_room_ensure(masterdb, room, mask != 0, mask, cptr->chatname);
      dict_add_bool(d, "room.has-vfos", mask != 0);
      dict_add_ulong(d, "room.vfo-mask", mask);
      dict_add_bool(d, "room.tx-control", false);
      dict_add_bool(d, "room.rx-tunable", ws_room_rx_tunable(room));
      dict_add_ulong(d, "room.rx-tuning-mask", ws_room_rx_tuning_mask(room));
      ws_broadcast_room_dict(NULL, d, room);

      for (rrconn_t *member = http_client_list ; member ; member = member->next) {
         if (member->authenticated && ws_client_in_room(member, room)) {
            media_send_available_all(member);
         }
      }

      char *controller = db_room_vfo_controller(masterdb, binding);
      ws_send_notice(cptr, "Room %s VFO %s %s (tuning controller: %s)",
         room, binding, strcasecmp(action, "add") == 0 ? "added" : "removed",
         controller ? controller : "none");
      free(controller);
      free(vfos);
   }
#else
   ws_send_error(cptr, "Room VFO mappings require database support");
#endif
   dict_free(d);
}

#ifdef USE_SQLITE
struct room_confirmation {
   rrconn_t *client;
   char session[HTTP_TOKEN_LEN + 1];
   char room[128];
   char token[7];
   time_t expires;
   bool force, history;
};
static struct room_confirmation room_confirmations[HTTP_MAX_SESSIONS];

/* Invalidate pending destructive-room confirmations for a closing session. */
static void rrserver_room_confirmation_close(const char *event, const char *data, rrconn_t *client, void *user) {
   for (size_t i = 0 ; i < HTTP_MAX_SESSIONS ; i++) {
      if (room_confirmations[i].client == client) {
         memset(&room_confirmations[i], 0, sizeof(room_confirmations[i]));
      }
   }
}

/* Reject deleted/unauthorized station rooms before admitting a join. */
static void rrserver_room_join_check(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   if (len != sizeof(rr_room_join_check_t)) {
      return;
   }
   rr_room_join_check_t *check = (rr_room_join_check_t *) data;
   bool exists, deleted;

   if (!db_room_status(masterdb, check->room, &exists, &deleted) || deleted ||
      (!exists && (!client || !client->authenticated || !client->user ||
      (ws_room_station_scoped(check->room) && !has_priv(client->user->uid, "admin|owner"))))) {
      check->allowed = false;
   } else if (!exists && !db_room_ensure(masterdb, check->room, false, 0, client->chatname)) {
      check->allowed = false;
   }
}
#endif

/* Validate session-bound confirmation before removing room metadata/history. */
void rrserver_handle_room_delete(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", NULL);

   if (room && (!strcasecmp(room, ws_site_room()) || rrserver_rig_room_configured(room))) {
      ws_send_error(cptr, "Site and configured rig rooms cannot be removed");
      dict_free(d);

      return;
   }
#ifdef USE_SQLITE

   bool force = dict_get_bool(d, "talk.force", false);
   bool history = dict_get_bool(d, "talk.history", false);
   bool exists, deleted;

   if (!cptr || !cptr->authenticated || !cptr->user || !has_priv(cptr->user->uid, "admin|owner") || !room ||
      strlen(room) >= sizeof(room_confirmations[0].room) ||
      !db_room_status(masterdb, room, &exists, &deleted) || !exists || (deleted && !force) || (history && !force)) {
      ws_send_error(cptr, "Room is unavailable for removal");
      dict_free(d);

      return;
   }
   struct room_confirmation *challenge = NULL, *available = NULL;

   for (size_t i = 0 ; i < HTTP_MAX_SESSIONS ; i++) {
      struct room_confirmation *entry = &room_confirmations[i];

      if (entry->expires <= now) {
         memset(entry, 0, sizeof(*entry));
      }

      if (!entry->client && !available) {
         available = entry;
      }

      if (entry->client == cptr) {
         challenge = entry;
      }
   }

   const char *token = dict_get(d, "talk.confirmation", NULL);

   if (!token) {
      if (!challenge) {
         challenge = available;
      }

      if (!challenge) {
         ws_send_error(cptr, "Unable to issue room removal confirmation");
      } else {
         challenge->client = cptr;
         snprintf(challenge->session, sizeof(challenge->session), "%s", cptr->token);
         snprintf(challenge->room, sizeof(challenge->room), "%s", room);
         snprintf(challenge->token, sizeof(challenge->token), "%06x", arc4random_uniform(0x1000000));
         challenge->expires = now + 300;
         challenge->force = force;
         challenge->history = history;

         if (!strcmp(dict_get(d, "talk.action", ""), "admin-remove")) {
            dict *row = dict_new();

            if (row) {
               dict_add(row, "room", room);
               dict_add(row, "confirmation", challenge->token);
               dict_add_bool(row, "deleted", deleted);
               rrserver_admin_send(cptr, "rooms", "confirm", row);
               dict_free(row);
            }
         } else {
            ws_send_notice(cptr, "To confirm, please use /room remove %s%s%s %s", room, force ? " --force" : "", history ? " --history" : "", challenge->token);
         }
      }
      dict_free(d);

      return;
   }

   if (!challenge || strcmp(challenge->session, cptr->token) || strcmp(challenge->room, room) ||
      strcmp(challenge->token, token) || challenge->force != force || challenge->history != history) {
      ws_send_error(cptr, "Invalid or expired confirmation; use /room remove %s again", room);
      dict_free(d);

      return;
   }
   memset(challenge, 0, sizeof(*challenge));
   dict_del(d, "talk.confirmation");

   if (!db_room_delete(masterdb, room, cptr->chatname, force, history)) {
      Log(LOG_WARN, "db", "failed to delete room metadata %s", room);
      ws_send_error(cptr, "Unable to remove room %s", room);
      dict_free(d);

      return;
   }
#else
   ws_send_error(cptr, "Room removal requires database support");
   dict_free(d);

   return;
#endif

   if (room) {
      if (ws_room_rig_namespace(room)) {
         ws_room_set_vfo_mask(room, 0);
      }
      ws_send_notice(cptr, "Room %s removed", room);
      ws_broadcast_room_dict(NULL, d, room);

      for (rrconn_t *cur = http_client_list ; cur ; cur = cur->next) {
         ws_client_part_room(cur, room);
      }
   }
#ifdef USE_SQLITE

   if (!strcmp(dict_get(d, "talk.action", ""), "admin-remove")) {
      rrserver_admin_send(cptr, "rooms", "changed", NULL);
   }
#endif
   dict_free(d);
}

/* Register this domain's server-owned protocol listeners. */
void rrserver_events_rooms_register(void) {
#ifdef USE_SQLITE
   event_on_binary(RR_ROOM_JOIN_CHECK_EVENT, rrserver_room_join_check, NULL);
   event_on(RR_OBJECT_CLOSE_EVENT, rrserver_room_confirmation_close, NULL);
#endif
   event_on("room.join", rrserver_handle_room_join, NULL);
   event_on("room.part", rrserver_handle_room_part, NULL);
   event_on("room.add", rrserver_handle_room_add, NULL);
   event_on("room.list", rrserver_handle_room_list, NULL);
   event_on("room.delete", rrserver_handle_room_delete, NULL);
   event_on("room.vfo-list", rrserver_handle_room_vfo_list, NULL);
   event_on("room.vfo", rrserver_handle_room_vfo, NULL);
   event_on("room.topic", rrserver_handle_room_topic, NULL);
}

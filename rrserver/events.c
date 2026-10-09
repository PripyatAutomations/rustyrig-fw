//
// rrserver/events.c: event listeners for shared protocol events from
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

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>

#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/rig.vfo.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
extern struct GlobalState rig;
#include <rrserver/ptt.h>
#include <librrprotocol/ws.mediachan.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>

extern void rrserver_media_register_events(void);   // media.c

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

   if (room && !db_room_ensure(masterdb, room, dict_get_bool(d, "room.has-vfos", false), (uint32_t)dict_get_ulong(d, "room.vfo-mask", 0), cptr ? cptr->chatname
      : "server") ) {
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

static void rrserver_handle_room_part(const char *event, const char *room, rrconn_t *client, void *user) {
   if (client && client->is_ptt && room && rig.ptt_rig) {
      const char *base = rr_rig_registry_room(rig.rigs, rig.ptt_rig);

      if (base && !strcasecmp(base, room) ) {
         rr_ptt_set_all_off_reason("left-tx-room");
      }
   }
}

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

   if (!db_room_set_topic(masterdb, room, topic) ) {
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
      dict_add(reply, "talk.rooms", rooms);
      free(rooms);
   }
#endif
   ws_send_dict(NULL, cptr, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

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

   if (!ws_room_name_valid(room) ||
      (ws_room_station_scoped(room) && !has_priv(cptr->user->uid, "admin|owner"))) {
      ws_send_error(cptr, "Station-scoped room creation requires admin or owner");
      dict_free(d);

      return;
   }

   if (ws_room_rig_base(room) ) {
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

   if (!db_room_restore(masterdb, room, cptr->chatname) ||
      (!exists && !db_room_ensure(masterdb, room, false, 0, cptr->chatname))) {
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
      ws_room_set_vfo_mask(room, mask);
   }
#endif
   ws_send_notice(cptr, "Room %s added", room);
   dict_free(d);
}

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
         char *vfos = requested_room && *requested_room ?
            db_room_vfo_list(masterdb, requested_room) : db_room_vfo_map_list(masterdb);
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

   if (action && !strcasecmp(action, "add") && !ws_room_rig_namespace(room) ) {
      ws_send_error(cptr, "VFO controls require a room in this site's numbered rig namespace");
      dict_free(d);

      return;
   }

   if (rrserver_rig_room_configured(room) ) {
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

         if (len < sizeof(alias) ) {
            memcpy(alias, binding, len);
            alias[len] = '\0';

            if (rr_rig_registry_find_alias(rig.rigs, alias) == radio) {
               char native[2] = {
                  (char)toupper( (unsigned char)dot[5]), 0
               };
               vfo = rr_server_vfo_find_alias(radio, native);
            }
         }
      }
   }

   if (!radio || !vfo || rr_server_vfo_owner(vfo) != radio) {
      ws_send_error(cptr, "VFO binding must belong to the rig named by the room");
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

      for (char *id = copy ? strtok_r(copy, " \t\r\n", &save) : NULL ; id ; id = strtok_r(NULL, " \t\r\n", &save) ) {
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
      dict_add_bool(d, "room.rx-tunable", ws_room_rx_tunable(room) );
      dict_add_ulong(d, "room.rx-tuning-mask", ws_room_rx_tuning_mask(room) );
      ws_broadcast_room_dict(NULL, d, room);

      for (rrconn_t *member = http_client_list ; member ; member = member->next) {
         if (member->authenticated && ws_client_in_room(member, room) ) {
            media_send_available_all(member);
         }
      }

      ws_send_notice(cptr, "Room %s VFO %s %s", room, binding, strcasecmp(action, "add") == 0 ? "added" : "removed");
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

static void rrserver_room_confirmation_close(const char *event, const char *data, rrconn_t *client, void *user) {
   for (size_t i = 0 ; i < HTTP_MAX_SESSIONS ; i++) {
      if (room_confirmations[i].client == client) {
         memset(&room_confirmations[i], 0, sizeof(room_confirmations[i]));
      }
   }
}

static void rrserver_room_join_check(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   if (len != sizeof(rr_room_join_check_t)) {
      return;
   }
   rr_room_join_check_t *check = (rr_room_join_check_t *)data;
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

static void rrserver_handle_room_delete(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", NULL);

   if (room && (!strcasecmp(room, ws_site_room() ) || rrserver_rig_room_configured(room) ) ) {
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
         ws_send_notice(cptr, "To confirm, please use /room remove %s%s%s %s", room, force ? " --force" : "", history ? " --history" : "", challenge->token);
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

   if (!db_room_delete(masterdb, room, cptr->chatname, force, history) ) {
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
   dict_free(d);
}

static void rrserver_handle_hello(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   dict_dump(d, NULL);
   dict_free(d);
}


static void rrserver_handle_nomatch(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   Log(LOG_WARN, "ws.nomatch", "NOMATCH: %s", data);
}

static void rrserver_handle_rigctlmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   dict_dump(d, NULL);

   const char *rc_cmd = dict_get(d, "rigctl.cmd", NULL);
   const char *rc_vfo = dict_get(d, "rigctl.vfo", NULL);
   const char *rc_from = dict_get(d, "rigctl.from", NULL);
   bool rc_ptt = dict_get_bool(d, "rigctl.ptt", false);
   int rc_freq = dict_get_int(d, "rigctl.freq", 0);
   const char *rc_mode = dict_get(d, "rigctl.mode", NULL);
   const char *rc_width = dict_get(d, "rigctl.width", NULL);
   float rc_power = dict_get_float(d, "rigctl.power", 0);

   Log(LOG_CRIT, "ws.rigctl", "cmd: %s, vfo: %s, from: %s, freq: %d, mode: %s, width: %s", rc_cmd, rc_vfo, rc_from, rc_freq, rc_mode ? rc_mode : "(none)",
      rc_width ? rc_width : "(none)");

   if (!rc_cmd || !rc_vfo || !rc_from) {
      dict_free(d);

      return;
   }

   rr_vfo_t vfo = vfo_lookup(rc_vfo[0]);

   const char *room = dict_get(d, "rigctl.room", ws_authoritative_room() );
   rr_server_rig_t *radio = rrserver_rig_for_room(room);

   if (!radio) {
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "ptt") == 0) {
      if (rc_ptt) {
         rig.ptt_rig = radio;
      }
      // Key/dekey the rig (from the PTT button in the client)
      Log(LOG_AUDIT, "rigctl", "User %s set PTT to %s on vfo %s", rc_from, (rc_ptt ? "true" : "false"), rc_vfo);

      if (rr_ptt_request(vfo, rc_ptt, rc_ptt ? "key-down" : "released") && cptr) {
         // The wire handler announced the request optimistically. Restore
         // session ownership when key-up fails; clear it when key-down fails.
         cptr->is_ptt = !rc_ptt;
         cptr->ptt_vfo = rc_ptt ? 0 : rc_vfo[0];
         snprintf(cptr->ptt_room, sizeof(cptr->ptt_room), "%s", rc_ptt ? "" : room);
         ws_send_userinfo(cptr, NULL);
         dict *state = dict_new();

         if (state) {
            dict_add(state, "msg.type", "cat");
            dict_add(state, "cat.cmd", "ptt");
            dict_add(state, "cat.room", room);
            dict_add(state, "cat.vfo", rc_vfo);
            dict_add(state, "cat.user", cptr->chatname);
            dict_add_bool(state, "cat.ptt", !rc_ptt);
            ws_broadcast_dict(NULL, state, WEBSOCKET_OP_TEXT);
            dict_free(state);
         }
         ws_send_error(cptr, "PTT request rejected (%s) for VFO %s in room %s by station safety controls or radio backend; check the server log for the cause",
            rc_ptt ? "start" : "stop", rc_vfo, room);
      }
      dict_free(d);

      return;
   }

   if (!strcasecmp(rc_cmd, "power")) {
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);

      if (!object || rr_backend_power_set_rig(radio, object, rc_power)) {
         if (cptr) {
            ws_send_error(cptr, "Unable to set power to %g watts on VFO %s in room %s", (double)rc_power, rc_vfo, room);
         }
      } else {
         Log(LOG_AUDIT, "rigctl", "User %s set room %s VFO %s POWER to %f watts", rc_from, room, rc_vfo, rc_power);
         rr_backend_poll_rig(radio, object);
      }
      dict_free(d);

      return;
   }

   if (radio != rr_rig_registry_default(rig.rigs) ) {
      rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, rc_vfo);
      rr_control_request_t request = {
         .rig = radio, .vfo = object, .source = "rigctl", .context = cptr
      };

      if (!strcmp(rc_cmd, "freq") ) {
         request.property = RR_PROP_VFO_FREQUENCY;
         request.value_type = VAL_LONG;
         request.value.l = rc_freq;
      } else if (!strcmp(rc_cmd, "mode") ) {
         request.property = RR_PROP_VFO_MODE;
         request.value_type = VAL_STR;
         request.value.s = rc_mode;
      } else if (!strcmp(rc_cmd, "width") ) {
         request.property = RR_PROP_VFO_WIDTH;
         request.value_type = VAL_LONG;
         request.value.l = rc_width ? strtol(rc_width, NULL, 10) : 0;
      }

      if (!object || !request.property || rr_rig_control(&request) != RR_CONTROL_OK) {
         Log(LOG_WARN, "rigctl", "Unable to apply %s in room %s", rc_cmd, room);
      } else {
         rr_backend_poll_rig(radio, object);
      }
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "mode") == 0) {
      // Set the rig mode (from !mode chat command or ws cat.cmd mode)
      if (!rc_mode) {
         Log(LOG_WARN, "ws.rigctl", "MODE set without a mode");
         dict_free(d);

         return;
      }

      rr_mode_t new_mode = vfo_parse_mode(rc_mode);

      if (new_mode == MODE_NONE) {
         Log(LOG_WARN, "ws.rigctl", "Couldn't parse mode %s", rc_mode);
         dict_free(d);

         return;
      }

      // Audit trail: who changed which VFO to what mode
      Log(LOG_AUDIT, "rigctl", "User %s set VFO %s MODE to %s", rc_from, rc_vfo, rc_mode);

      // Confirm the new values for UUID-backed clients immediately, including width changes caused by mode.
      if (!rr_set_mode(vfo, new_mode)) {
         rr_be_poll(vfo);
      }
      dict_free(d);

      return;
   }

   if (strcasecmp(rc_cmd, "width") == 0) {
      // Set the rig passband width (from !width chat command or ws cat.cmd width)
      if (!rc_width) {
         Log(LOG_WARN, "ws.rigctl", "WIDTH set without a width");
         dict_free(d);

         return;
      }

      // Audit trail: who changed which VFO to what passband width
      Log(LOG_AUDIT, "rigctl", "User %s set VFO %s WIDTH to %s", rc_from, rc_vfo, rc_width);

      if (!rr_set_width(vfo, rc_width)) {
         rr_be_poll(vfo);
      }
      dict_free(d);

      return;
   }

   fprintf(stderr, "setting vfo %s freq to %d\n", rc_vfo, rc_freq);

   // Audit trail: who changed which VFO to what frequency
   Log(LOG_AUDIT, "rigctl", "User %s set VFO %s FREQ to %d hz", rc_from, rc_vfo, rc_freq);

   if (!rr_freq_set(vfo, rc_freq)) {
      rr_be_poll(vfo);
   }
   dict_free(d);
}

/* The generic property event is currently an internal observation stream. Register it explicitly so it does not fall through the NOMATCH warning handler while
 * the future generic client protocol adapter is still pending. */
static void rrserver_handle_rig_property_changed(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   Log(LOG_CRAZY, "rig.property", "%s: %s", event, data ? data : "(null)");
}


// Ask the backend to poll the active VFO, e.g. after the active VFO changes
// (!vfo), so the new VFO's cat.state gets broadcast promptly.
static void rrserver_handle_be_poll(const char *event, const char *data, rrconn_t *cptr, void *user) {
   rr_be_poll(active_vfo);
}


static void rrserver_handle_send_chat_replay(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data || !cptr) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   dict_dump(d, NULL);

   const char *channel = dict_get(d, "talk.target", NULL);

#ifdef USE_SQLITE

   if (channel && ws_client_in_room(cptr, channel)) {
      db_send_chat_replay(cptr, channel);
   }
#endif

   // The initial ping: send it now that login, CAT state, media negotiation,
   // and the chat replay have all been pushed, so the client is settled and
   // the RTT measurement reflects the real link instead of UI-setup lag.
   ws_send_ping(cptr);
   dict_free(d);
}


static void rrserver_handle_talkmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   Log(LOG_CRAZY, "events.ws", "ENTER talk.msg: event=<%s> data=<%p> cptr=<%p>", event ? event : "(null)", data, cptr);

   if (!data || !cptr) {
      Log(LOG_CRIT, "ws.chat", "handle_talkmsg with data:<%p> and cptr:<%p>", data, cptr);

      return;
   }

   dict *d = json2dict(data);
   Log(LOG_CRAZY, "events.ws", "talk.msg json2dict returned d=<%p>", d);

   if (!d) {
      Log(LOG_WARN, "ws.chat", "failed to parse talk.msg event");

      return;
   }

   const char *channel = dict_get(d, "talk.target", NULL);

   const char *msg_type = dict_get(d, "talk.msg_type", NULL);

   if (!msg_type) {
      Log(LOG_WARN, "ws.chat", "talk.msg event has no message type");
      dict_free(d);

      return;
   }

   /*
    * File chunks aren't normal chat messages and should not be written to chat_log. They still need to be broadcast.
    */
   if (strcasecmp(msg_type, "file_chunk") == 0) {
      Log(LOG_DEBUG, "ws.chat", "broadcasting file chunk from %s", cptr->chatname);
      ws_broadcast_room_dict(cptr, d, channel);
      dict_free(d);

      return;
   }

   if (strcasecmp(msg_type, "priv") == 0 || strcasecmp(msg_type, "privmsg") == 0) {
      const char *target_name = dict_get(d, "talk.target", NULL);
      rrconn_t *target = target_name ? http_find_client_by_name(target_name) : NULL;

      if (!target) {
         ws_send_error(cptr, "No such user: %s", target_name ? target_name : "(none)");
         dict_free(d);

         return;
      }
#ifdef USE_SQLITE

      if (!db_add_chat_msg(masterdb, now, cptr->chatname, target_name, "privmsg", dict_get(d, "talk.data", "") ) ) {
         Log(LOG_WARN, "db", "failed to save private message");
      }
#endif
      dict_add(d, "talk.msg_type", "priv");
      ws_send_dict(cptr, target, d, WEBSOCKET_OP_TEXT);

      if (target != cptr) {
         ws_send_dict(cptr, cptr, d, WEBSOCKET_OP_TEXT);
      }
      dict_free(d);

      return;
   }

   /*
    * Normal public/action messages.
    */
   if (strcasecmp(msg_type, "pub") == 0 ||
      strcasecmp(msg_type, "action") == 0) {

      if (strcasecmp(msg_type, "action") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s * %s%s", channel ? channel : ws_authoritative_room(), cptr->chatname, dict_get(d, "talk.data", "") );
      } else if (strcasecmp(msg_type, "pub") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s <%s> %s", channel ? channel : ws_authoritative_room(), cptr->chatname, dict_get(d, "talk.data", "") );
      }

      const char *talk_from = dict_get(d, "talk.from", NULL);
      const char *talk_target = dict_get(d, "talk.target", NULL);
      const char *talk_msg_type = dict_get(d, "talk.msg_type", NULL);
      const char *talk_msg = dict_get(d, "talk.data", NULL);

#ifdef USE_SQLITE

      if (channel) {
         if (!db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, talk_msg) ) {
            Log(LOG_WARN, "db", "failed to save chat message");
         }
      }
#endif

      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcasting: from=<%s> target=<%s> type=<%s>", talk_from, talk_target, talk_msg_type);

      ws_broadcast_room_dict(cptr, d, channel);
      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcast returned");
      dict_free(d);

      return;
   }

   Log(LOG_DEBUG, "ws.chat", "unknown talk.msg type: %s", msg_type);
   dict_free(d);
}


static bool rrserver_recording_control(const char *data, bool start) {
   if (!data) {
      Log(LOG_WARN, "record", "recording-%s without channel data", start ? "start" : "stop");

      return true;
   }

   dict *d = json2dict(data);

   if (!d) {
      Log(LOG_WARN, "record", "recording-%s with unparseable data", start ? "start" : "stop");

      return true;
   }

   const char *uuid = dict_get(d, "media.chan-uuid", NULL);

   if (!uuid) {
      uuid = dict_get(d, "recording.chan-uuid", NULL);
   }
   struct rr_mediachan *channel = uuid ? media_chan_find_uuid(uuid) : NULL;

   if (!channel) {
      Log(LOG_WARN, "record", "recording-%s for unknown media channel %s", start ? "start" : "stop", (uuid ? uuid : "<none>") );
      dict_free(d);

      return true;
   }

   const char *codec = dict_get(d, "media.codec", NULL);

   if (!codec || strlen(codec) != 4) {
      codec = channel->codec;
   }

   if (!codec || strlen(codec) != 4) {
      Log(LOG_WARN, "record", "recording-%s: channel %s has no active codec", start ? "start" : "stop", channel->uuid);
      dict_free(d);

      return true;
   }

   bool fwdsp_tx = (channel->direction == RR_BINFRAME_DIR_RX);
   rrconn_t *talker = whos_talking();
   const char *recording_id = (!fwdsp_tx && start) ?
      rr_ptt_recording_id( (rr_vfo_t)channel->vfo) : NULL;
   const char *who = fwdsp_tx ? "radio" :
      (talker && talker->ptt_vfo == 'A' + channel->vfo ? talker->chatname : NULL);

   if (start && (!who || !*who) ) {
      Log(LOG_WARN, "record", "No transmitter for recording on channel %s", channel->uuid);
      dict_free(d);

      return true;
   }
   const char *record_file = (!fwdsp_tx && channel->vfo >= 0) ?
      rr_ptt_recording_file( (rr_vfo_t)channel->vfo) : NULL;
   bool failed = start ?
      fwdsp_cmd_start_record_named_file(codec, fwdsp_tx, channel->uuid, who, !fwdsp_tx, recording_id, record_file) :
      fwdsp_cmd_stop_record_channel(codec, fwdsp_tx, channel->uuid);

   if (failed) {
      Log(LOG_WARN, "record", "Unable to %s recording for %s (%s.%s)", start ? "start" : "stop", channel->uuid, codec, fwdsp_tx ? "tx" : "rx");
   } else {
      Log(LOG_INFO, "record", "%s recording for %s (%s.%s)", start ? "Started" : "Stopped", channel->uuid, codec, fwdsp_tx ? "tx" : "rx");
   }

   dict_free(d);

   return failed;
}

static void rrserver_handle_recording_start(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   rrserver_recording_control(data, true);
}

static void rrserver_handle_recording_stop(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   rrserver_recording_control(data, false);
}


static void rrserver_handle_send_cat_state(const char *event, const char *data, rrconn_t *cptr, void *user) {
   // Push the current rig state to a single (usually just-logged-in) client
   if (!cptr) {
      return;
   }
   rr_cat_state_send(cptr);
}


// librrprotocol emits "ptt.off" to force TX off (srv.chat.c MUTE, srv.rigctl.c
// admin noob-halt). NB: nobody registered for this before, so the event fell
// through to NOMATCH and the rig kept transmitting while the flag was cleared.
static void rrserver_handle_ptt_off(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)data;
   (void)cptr;
   (void)user;
   Log(LOG_AUDIT, "ptt", "Forced TX off (%s event)", (event ? event : "ptt.off") );
   rr_ptt_set_all_off_reason("forced");
}


// A departing user was holding PTT (fired from srv.http.c on MG_EV_CLOSE).
// Release only the VFO this user keyed; we have no business touching any
// other VFO's TX state. (ptt_vfo is recorded on every key-up, so unknown
// should only happen for stale/foreign sessions.)
static void rrserver_handle_rig_ptt_off(const char *event, const char *data, rrconn_t *cptr, void *user) {
   dict *d = (data ? json2dict(data) : NULL);
   const char *who = (d ? dict_get(d, "cat.user", NULL) : NULL);
   const char *vfo = (d ? dict_get(d, "cat.vfo", NULL) : NULL);

   if (vfo && vfo[0]) {
      Log(LOG_AUDIT, "rigctl", "Departing user %s had PTT on vfo %s: keying down", (who ? who : "(unknown)"), vfo);

      if (rr_ptt_request(vfo_lookup(vfo[0]), false, "disconnect") && cptr) {
         // A takeover must stop if the backend cannot release the old holder.
         cptr->is_ptt = true;
         cptr->ptt_vfo = vfo[0];
         ws_send_userinfo(cptr, NULL);
      }
   } else {
      Log(LOG_WARN, "rigctl", "Departing user %s held PTT but no VFO recorded; NOT touching rig TX", (who ? who : "(unknown)") );
   }

   if (d) {
      dict_free(d);
   }
}


// Server measured RTT to a client from the ping/pong exchange; store/log it
// so the audio subsystem (or anything else) can track link quality.
static void rrserver_handle_latency(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!cptr || !data) {
      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   long long rtt = dict_get_llong(d, "latency.rtt", -1);
   long long rtt_us = dict_get_llong(d, "latency.rtt_us", -1);

   if (rtt >= 0) {
      Log(LOG_DEBUG, "latency", "RTT to user %s: %lld ms (%lld us)", cptr->chatname, rtt, rtt_us);
   }
   dict_free(d);
}


/*
 * librrprotocol emits "authdb.load" when net.http.authdb-dynamic is true (see srv.auth.passdb.c: http_reload_users()); we fill http_users[] from the sqlite
 * users table here.
 */
static void rrserver_handle_authdb_load(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;

   if (!masterdb) {
      Log(LOG_CRIT, "db", "authdb-dynamic is set but masterdb isn't open!");

      return;
   }

   if (db_get_users(masterdb) < 0) {
      Log(LOG_CRIT, "db", "Failed to load users from database; keeping previous table");
   }
}

/*
 * A client (with admin/owner privs -- checked in srv.http.c) asked us to rehash: reload the config file and then the user database (which may come from the
 * sqlite users table or the http.users file depending on net.http.authdb-dynamic).
 */
static void rrserver_handle_rehash(const char *event, const char *data, rrconn_t *cptr, void *user) {
   Log(LOG_INFO, "core", "Rehashing server configuration (requested by %s)", (cptr && cptr->chatname[0] != '\0' ? cptr->chatname : "internal") );

   if (!cfg_reload(NULL) ) {
      Log(LOG_CRIT, "core", "Config reload failed; keeping previous configuration");
   }

   // Reload users after the config, since authdb path/dynamic may have changed
   int users = http_reload_users();

   if (users < 0) {
      Log(LOG_CRIT, "auth", "User database reload failed; keeping previous users");
   } else {
      Log(LOG_INFO, "auth", "User database reloaded: %d users", users);
   }
}

/*
 * A client (with admin/owner privs -- checked in srv.chat.c) ran /quota:
 *   LIST | SHOW <user>... | ADD <user> <minutes> | RESET <user>... |
 *   SET <user> <minutes>
 * Replies to the requesting client via ws_send_notice().
 */
static void quota_reply(rrconn_t *cptr, const char *fmt, ...) {
   char buf[HTTP_WS_MAX_MSG + 1];
   va_list ap;

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   ws_send_notice(cptr, "%s", buf);
}

// db_quota_list callback: print one row to the requesting client
static int quota_list_cb(const char *name, int credits, void *user) {
   quota_reply( (rrconn_t *)user, "  %-16s %s", name, time_t2dhms(credits) );

   return 1;
}

static void quota_apply(rrconn_t *cptr, const char *actor, const char *subcmd, int argc, char **argv) {
   if (!masterdb || !subcmd) {
      return;
   }

   if (strcasecmp(subcmd, "LIST") == 0) {
      quota_reply(cptr, "TX quotas (time remaining):");
      db_quota_list(masterdb, quota_list_cb, cptr);

      return;
   }

   if (strcasecmp(subcmd, "HELP") == 0) {
      quota_reply(cptr, "Usage: /quota LIST | SHOW <user>... | ADD <user> <time> | RESET <user>... | SET <user> <time>");
      quota_reply(cptr, "  ADD/SET take a dhms time string like 30m, 2h, 1d or 1w2d (0 = no TX allowed);");
      quota_reply(cptr, "  SHOW shows exact seconds too.");

      return;
   }

   if (strcasecmp(subcmd, "SHOW") == 0 || strcasecmp(subcmd, "RESET") == 0) {
      if (argc < 1) {
         quota_reply(cptr, "quota %s: no user given", subcmd);

         return;
      }

      for (int i = 0 ; i < argc ; i++) {
         const char *name = argv[i];
         int before = db_quota_get(masterdb, name);

         if (strcasecmp(subcmd, "SHOW") == 0) {
            int mins = (before < 0 ? 0 : before) / 60;

            quota_reply(cptr, "%s: %s remaining", name, time_t2dhms(before) );
         } else {
            if (db_quota_set(masterdb, name, 60 * 60) ) {
               Log(LOG_AUDIT, "quota", "%s reset %s's TX quota to 60m (was %s)", actor, name, time_t2dhms(before) );
               quota_reply(cptr, "%s: reset to 60m", name);
               quota_reset_warned(name);
            } else {
               quota_reply(cptr, "quota RESET: failed for %s", name);
            }
         }
      }

      return;
   }

   if (strcasecmp(subcmd, "ADD") == 0 || strcasecmp(subcmd, "SET") == 0) {
      if (argc < 2) {
         quota_reply(cptr, "Usage: /quota %s <user> <time> [user2 time2 ...] (time like 30m, 2h, 1d)", subcmd);

         return;
      }

      // Pairs: user time [user time ...]; time is a dhms string (see
      // librustyaxe/util.time.c dhms2time_t), e.g. 90m, 2h, 1d, 1w2d
      for (int i = 0 ; i + 1 < argc ; i += 2) {
         const char *name = argv[i];
         time_t secs = dhms2time_t(argv[i + 1]);
         int before = db_quota_get(masterdb, name);

         if (secs <= 0 && argv[i + 1][0] != '0') {
            quota_reply(cptr, "quota %s: invalid time '%s' for %s (try 30m, 2h, 1d)", subcmd, argv[i + 1], name);

            return;
         }

         if (secs <= 0 && strcasecmp(subcmd, "ADD") == 0) {
            // ADD 0 is a no-op (would just leave credits unchanged)
            quota_reply(cptr, "quota ADD: nothing to add for %s (got 0)", name);
            continue;
         }
         bool ok;

         if (strcasecmp(subcmd, "ADD") == 0) {
            ok = db_quota_add(masterdb, name, (int)secs);

            if (ok) {
               char *was = time_t2dhms( (time_t)(before < 0 ? 0 : before) );
               char *added = time_t2dhms(secs);
               char *now = time_t2dhms( (time_t)db_quota_get(masterdb, name) );
               Log(LOG_AUDIT, "quota", "%s added %s to %s's TX quota (was %s)", actor, added, name, was);
               quota_reply(cptr, "added %s (now %s) to %s's quota", added, now, name);
               free( (void *)was);
               free( (void *)added);
               free( (void *)now);
            }
         } else {
            ok = db_quota_set(masterdb, name, (int)secs);

            if (ok) {
               char *was = time_t2dhms( (time_t)(before < 0 ? 0 : before) );
               char *set = time_t2dhms(secs);
               Log(LOG_AUDIT, "quota", "%s set %s's TX quota to %s (was %s)", actor, name, set, was);
               quota_reply(cptr, "%s set %s's quota to %s", actor, name, set);
               free( (void *)was);
               free( (void *)set);
            }
         }

         if (!ok) {
            quota_reply(cptr, "quota %s: failed for %s", subcmd, name);
         } else {
            quota_reset_warned(name);
         }
      }

      if (argc % 2 != 0) {
         quota_reply(cptr, "quota %s: dangling argument '%s' (expected user time pairs)", subcmd, argv[argc - 1]);
      }

      return;
   }

   quota_reply(cptr, "Unknown quota subcommand: %s (try LIST, SHOW, ADD, RESET, SET)", subcmd);
}

static void rrserver_handle_quota_cmd(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!cptr || !data) {
      return;
   }

   if (!masterdb) {
      ws_send_error(cptr, "quota: database is not open");

      return;
   }

   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   // Command layout: clients put the subcommand in talk.target (LIST/SHOW/
   // ADD/RESET/SET) with the rest of the args in talk.data. A non-keyword
   // target with no data is a single-user SHOW shortcut.
   char tail[HTTP_WS_MAX_MSG + 1];
   const char *data_str = dict_get(d, "quota.data", "");
   const char *target = dict_get(d, "quota.target", NULL);
   bool target_is_cmd = target &&
      (strcasecmp(target, "LIST") == 0 || strcasecmp(target, "SHOW") == 0 ||
         strcasecmp(target, "ADD") == 0 || strcasecmp(target, "RESET") == 0 ||
         strcasecmp(target, "SET") == 0 || strcasecmp(target, "HELP") == 0);

   if (target_is_cmd) {
      snprintf(tail, sizeof(tail), "%s %s", target, data_str ? data_str : "");
   } else if ( (!data_str || data_str[0] == '\0') && target) {
      snprintf(tail, sizeof(tail), "SHOW %s", target);
   } else {
      snprintf(tail, sizeof(tail), "%s", data_str ? data_str : "");
   }

   // Split into subcommand + args (users and/or minutes)
   char *argv[32];
   int argc = 0;
   char *p = tail;

   while (p && *p && argc < 32) {
      while (*p == ' ' || *p == '\t') {
         p++;
      }

      if (*p == '\0') {
         break;
      }
      argv[argc++] = p;
      while (*p && *p != ' ' && *p != '\t') {
         p++;
      }

      if (*p) {
         *p++ = '\0';
      }
   }

   if (argc < 1) {
      // Bare /quota is a shortcut for LIST; use /quota help for the help text
      quota_apply(cptr, cptr->chatname, "LIST", 0, NULL);
      dict_free(d);

      return;
   }

   quota_apply(cptr, cptr->chatname, argv[0], argc - 1, &argv[1]);
   dict_free(d);
}

static void user_reply(rrconn_t *cptr, const char *fmt, ...) {
   char buf[HTTP_WS_MAX_MSG + 1];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   ws_send_notice(cptr, "%s", buf);
}

static bool user_name_valid(const char *name) {
   if (!name || !*name || strlen(name) > HTTP_USER_LEN) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)name ; *p ; p++) {
      if (!isalnum(*p) && *p != '_' && *p != '-') {
         return false;
      }
   }

   return true;
}

/* Privileges are stored as a comma-separated list. Keep the list deliberately conservative here because this command changes authorization state. */
static bool user_privilege_list_valid(const char *privileges, bool allow_empty) {
   if (!privileges) {
      return false;
   }

   if (!*privileges) {
      return allow_empty;
   }

   if (strlen(privileges) > USER_PRIV_LEN) {
      return false;
   }

   const char *p = privileges;
   while (*p) {
      const char *end = strchr(p, ',');
      size_t len = end ? (size_t)(end - p) : strlen(p);

      if (len == 0 || len >= 64) {
         return false;
      }

      for (size_t i = 0 ; i < len ; i++) {
         unsigned char ch = (unsigned char)p[i];

         if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '-' && ch != '*') {
            return false;
         }
      }

      if (end && end[1] == '\0') {
         return false;
      }
      p = end ? end + 1 : p + len;
   }
   return true;
}

static bool user_privilege_contains(const char *privileges, const char *wanted) {
   if (!privileges || !wanted || !*wanted) {
      return false;
   }
   char copy[USER_PRIV_LEN + 1];
   strlcpy(copy, privileges, sizeof(copy) );
   char *save = NULL;

   for (char *token = strtok_r(copy, ",", &save) ; token ;
      token = strtok_r(NULL, ",", &save) ) {
      if (strcasecmp(token, wanted) == 0) {
         return true;
      }
   }

   return false;
}

static bool user_privilege_has_elevated(const char *privileges) {
   return user_privilege_contains(privileges, "admin") ||
          user_privilege_contains(privileges, "owner");
}

static bool user_privilege_add_tokens(const char *base, const char *extra, char *out, size_t out_len) {
   if (!base || !extra || !out || out_len == 0) {
      return false;
   }
   snprintf(out, out_len, "%s", base);
   char copy[USER_PRIV_LEN + 1];
   strlcpy(copy, extra, sizeof(copy) );
   char *save = NULL;

   for (char *token = strtok_r(copy, ",", &save) ; token ;
      token = strtok_r(NULL, ",", &save) ) {
      if (user_privilege_contains(out, token) ) {
         continue;
      }
      size_t used = strlen(out);
      int written = snprintf(out + used, out_len - used, "%s%s", used ? "," : "", token);

      if (written < 0 || (size_t)written >= out_len - used) {
         return false;
      }
   }

   return true;
}

static bool user_privilege_remove_tokens(const char *base, const char *remove, char *out, size_t out_len) {
   if (!base || !remove || !out || out_len == 0) {
      return false;
   }
   out[0] = '\0';
   char base_copy[USER_PRIV_LEN + 1];
   char remove_copy[USER_PRIV_LEN + 1];
   strlcpy(base_copy, base, sizeof(base_copy) );
   strlcpy(remove_copy, remove, sizeof(remove_copy) );
   char *save = NULL;

   for (char *token = strtok_r(base_copy, ",", &save) ; token ;
      token = strtok_r(NULL, ",", &save) ) {
      if (user_privilege_contains(remove_copy, token) ) {
         continue;
      }
      size_t used = strlen(out);
      int written = snprintf(out + used, out_len - used, "%s%s", used ? "," : "", token);

      if (written < 0 || (size_t)written >= out_len - used) {
         return false;
      }
   }

   return true;
}

static bool user_is_elevated(const http_user_t *user) {
   return user && user_privilege_has_elevated(user->privs);
}

static bool user_target_allowed(const http_user_t *actor, const http_user_t *target, bool password_change) {
   if (!actor || !target) {
      return false;
   }

   if (has_priv(actor->uid, "owner") ) {
      return true;
   }

   if (user_is_elevated(target) ) {
      return password_change && actor->uid == target->uid;
   }

   return true;
}

static bool user_temp_password(char *password, size_t length) {
   if (!password || length < 9) {
      return false;
   }
   unsigned value = arc4random_uniform(100000000U);

   return snprintf(password, length, "%08u", value) > 0;
}

static bool user_reload_database(rrconn_t *cptr) {
   if (db_get_users(masterdb) < 0) {
      ws_send_error(cptr, "USER: account database reload failed");

      return false;
   }

   return true;
}

static void user_disconnect_sessions(const http_user_t *target, const char *reason) {
   if (!target) {
      return;
   }

   for (rrconn_t *cur = http_client_list ; cur ; ) {
      rrconn_t *next = cur->next;

      if (cur->user == target) {
         ws_kick_client(cur, reason);
      }
      cur = next;
   }
}

static void rrserver_handle_user_cmd(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;
#ifndef USE_SQLITE
   (void)data;

   if (cptr) {
      ws_send_error(cptr, "USER: account management requires SQLite support");
   }

   return;
#else

   if (!cptr || !cptr->user || !data) {
      return;
   }

   if (!masterdb) {
      ws_send_error(cptr, "USER: account database is not open");

      return;
   }

   /* user.cmd is emitted with event_emit_dict(), so data is the serialized event envelope.  Parse that envelope before splitting the command tail;
    * tokenizing the JSON itself made every subcommand look like a missing or unknown account (for example `/user list` became an empty target). */
   dict *request = json2dict(data);

   if (!request) {
      ws_send_error(cptr, "USER: invalid command payload");

      return;
   }
   const char *command_data = dict_get(request, "user.data", NULL);

   if (!command_data) {
      command_data = "";
   }

   char input[HTTP_WS_MAX_MSG + 1];
   snprintf(input, sizeof(input), "%s", command_data);
   dict_free(request);
   char *argv[8] = {
      0
   };
   int argc = 0;
   char *save = NULL;

   for (char *token = strtok_r(input, " \t\r\n", &save) ;
      token && argc < (int)(sizeof(argv) / sizeof(argv[0]) ) ;
      token = strtok_r(NULL, " \t\r\n", &save) ) {
      argv[argc++] = token;
   }

   bool self_password = argc == 3 && !strcasecmp(argv[0], "pass") &&
      !strcasecmp(argv[1], cptr->user->name);

   if (!has_priv(cptr->user->uid, "admin|owner") && !self_password) {
      ws_send_error(cptr, "Account administration requires admin or owner; use /user pass <your-user> <password> for your own password");

      return;
   }

   if (argc == 0 || strcasecmp(argv[0], "help") == 0) {
      user_reply(cptr,
         "Usage: /user list | add <user> [privileges] | remove <user> | lock <user> | unlock <user> | privs <user> list|add|remove|set [privileges] | "
         "oldpw | resetpw <user> | pass <user> <password>");

      return;
   }

   if (strcasecmp(argv[0], "list") == 0) {
      if (argc != 1) {
         user_reply(cptr, "Usage: /user list");

         return;
      }
      user_reply(cptr, "Users:");

      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         http_user_t *entry = &http_users[i];

         if (!entry->name[0]) {
            continue;
         }
         char expiry[32] = "never";

         if (entry->password_expires > 0) {
            struct tm tm_value;
            localtime_r(&entry->password_expires, &tm_value);
            strftime(expiry, sizeof(expiry), "%Y-%m-%d", &tm_value);
         }
         user_reply(cptr, "  %-16s %-7s sessions=%d privs=%s password-expires=%s%s", entry->name, entry->enabled ? "enabled" : "locked", entry->sessions, entry
            ->privs[0] ? entry->privs : "none", expiry, entry->password_change_required ? " (change required)" : "");
      }

      return;
   }

   if (strcasecmp(argv[0], "oldpw") == 0) {
      if (argc != 1) {
         user_reply(cptr, "Usage: /user oldpw");

         return;
      }
      int configured_age = cfg_get_int("security.max-pw-age", 90);
      unsigned max_age = configured_age > 0 ? (unsigned)configured_age : 0;

      if (max_age == 0) {
         user_reply(cptr, "USER: password age reporting is disabled");

         return;
      }
      time_t cutoff = now - (time_t)max_age * 86400;
      int found = 0;

      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         http_user_t *entry = &http_users[i];

         if (!entry->name[0] || entry->password_set <= 0 || entry->password_set > cutoff) {
            continue;
         }
         int days = (int)( (now - entry->password_set) / 86400);
         user_reply(cptr, "  %-16s password set %d days ago%s", entry->name, days, entry->password_change_required ? " (change required)" : "");
         found++;
      }

      if (!found) {
         user_reply(cptr, "USER: no passwords are older than %u days", max_age);
      }

      return;
   }

   if (strcasecmp(argv[0], "add") == 0) {
      if (argc < 2 || argc > 3 || !user_name_valid(argv[1]) ) {
         user_reply(cptr, "Usage: /user add <user> [privileges]");

         return;
      }

      if (http_getuid(argv[1]) >= 0) {
         user_reply(cptr, "USER: account already exists: %s", argv[1]);

         return;
      }
      const char *privileges = argc == 3 ? argv[2] : "view,chat";

      if (strlen(privileges) > USER_PRIV_LEN ||
         (!has_priv(cptr->user->uid, "owner") && user_privilege_has_elevated(privileges) ) ) {
         user_reply(cptr, "USER: administrators cannot create owner or administrator accounts");

         return;
      }
      int uid = db_user_next_uid(masterdb);

      if (uid < 1 || uid >= HTTP_MAX_USERS) {
         user_reply(cptr, "USER: no user slots are available");

         return;
      }
      char password[9];
      char *password_hash = NULL;
      bool ok = user_temp_password(password, sizeof(password) );

      if (ok) {
         password_hash = hash_passwd(password);
      }
      ok = ok && password_hash && db_user_create(masterdb, uid, argv[1], true, password_hash, "no@example.com", 1, privileges, true, now + 7 * 86400);
      free(password_hash);

      if (!ok || !user_reload_database(cptr) ) {
         user_reply(cptr, "USER: failed to add %s", argv[1]);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s added user %s with privileges %s", cptr->chatname, argv[1], privileges);
      user_reply(cptr, "USER: added %s with temporary password %s (password change required at next login)", argv[1], password);

      return;
   }

   const char *target_name = argc > 1 ? argv[1] : NULL;
   int target_uid = target_name ? http_getuid(target_name) : -1;
   http_user_t *target = target_uid >= 0 ? &http_users[target_uid] : NULL;
   bool password_change = strcasecmp(argv[0], "pass") == 0 || strcasecmp(argv[0], "resetpw") == 0;

   if (!target_name || !target || !target->name[0]) {
      user_reply(cptr, "USER: account not found: %s", target_name ? target_name : "(missing)");

      return;
   }

   if (strcasecmp(argv[0], "privs") == 0) {
      if (argc < 3 || argc > 4 ||
         (strcasecmp(argv[2], "list") != 0 && argc != 4) ||
         (argc == 4 && strcasecmp(argv[2], "list") == 0) ) {
         user_reply(cptr, "Usage: /user privs <user> list|add|remove|set [privileges]");

         return;
      }

      bool actor_is_owner = has_priv(cptr->user->uid, "owner");

      if (strcasecmp(argv[2], "list") == 0) {
         user_reply(cptr, "USER: %s privileges: %s", target->name, target->privs[0] ? target->privs : "none");

         return;
      }

      if (user_is_elevated(target) && !actor_is_owner) {
         user_reply(cptr, "USER: only owners may change administrator or owner privileges");

         return;
      }

      const char *requested = argv[3];

      if (!user_privilege_list_valid(requested, false) ||
         (strcasecmp(requested, "none") == 0 && strcasecmp(argv[2], "set") != 0) ) {
         user_reply(cptr, "USER: invalid privilege list: %s", requested ? requested : "(missing)");

         return;
      }

      char updated[USER_PRIV_LEN + 1];

      if (strcasecmp(argv[2], "set") == 0) {
         if (strcasecmp(requested, "none") == 0) {
            updated[0] = '\0';
         } else {
            strlcpy(updated, requested, sizeof(updated) );
         }
      } else if (strcasecmp(argv[2], "add") == 0) {
         if (!user_privilege_add_tokens(target->privs, requested, updated, sizeof(updated) ) ) {
            user_reply(cptr, "USER: resulting privilege list is too long");

            return;
         }
      } else if (strcasecmp(argv[2], "remove") == 0) {
         if (!user_privilege_remove_tokens(target->privs, requested, updated, sizeof(updated) ) ) {
            user_reply(cptr, "USER: resulting privilege list is too long");

            return;
         }
      } else {
         user_reply(cptr, "Usage: /user privs <user> list|add|remove|set [privileges]");

         return;
      }

      if (!actor_is_owner && user_privilege_has_elevated(updated) ) {
         user_reply(cptr, "USER: administrators cannot grant owner or administrator privileges");

         return;
      }

      if (!db_user_set_privileges(masterdb, target->name, updated) ||
         !user_reload_database(cptr) ) {
         user_reply(cptr, "USER: failed to update privileges for %s", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s changed privileges for %s to %s", cptr->chatname, target->name, updated[0] ? updated : "none");
      user_reply(cptr, "USER: %s privileges for %s: %s", argv[2], target->name, updated[0] ? updated : "none");

      return;
   }

   if (!user_target_allowed(cptr->user, target, password_change) ) {
      user_reply(cptr, "USER: insufficient privilege to modify %s", target->name);

      return;
   }

   if (strcasecmp(argv[0], "lock") == 0 || strcasecmp(argv[0], "unlock") == 0) {
      if (argc != 2 || (user_is_elevated(target) && !has_priv(cptr->user->uid, "owner") ) ) {
         user_reply(cptr, "USER: admins cannot lock owners or administrators");

         return;
      }

      if (target == cptr->user) {
         user_reply(cptr, "USER: you cannot lock or unlock your own account");

         return;
      }
      bool enabled = strcasecmp(argv[0], "unlock") == 0;

      if (!db_user_set_enabled(masterdb, target->name, enabled) || !user_reload_database(cptr) ) {
         user_reply(cptr, "USER: failed to %s %s", enabled ? "unlock" : "lock", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s %s user %s", cptr->chatname, enabled ? "unlocked" : "locked", target->name);
      user_reply(cptr, "USER: %s %s", enabled ? "Unlocked" : "Locked", target->name);

      return;
   }

   if (strcasecmp(argv[0], "remove") == 0) {
      if (argc != 2 || (user_is_elevated(target) && !has_priv(cptr->user->uid, "owner") ) ) {
         user_reply(cptr, "USER: admins cannot remove owners or administrators");

         return;
      }

      if (target == cptr->user) {
         user_reply(cptr, "USER: you cannot remove your own account");

         return;
      }
      char removed_name[HTTP_USER_LEN + 1];
      strlcpy(removed_name, target->name, sizeof(removed_name) );

      if (!db_user_remove(masterdb, removed_name) ) {
         user_reply(cptr, "USER: failed to remove %s", removed_name);

         return;
      }
      user_disconnect_sessions(target, "Your account was removed by an administrator");

      if (!user_reload_database(cptr) ) {
         user_reply(cptr, "USER: removed %s, but account reload failed", removed_name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s removed user %s", cptr->chatname, removed_name);
      user_reply(cptr, "USER: removed %s", removed_name);

      return;
   }

   if (strcasecmp(argv[0], "resetpw") == 0) {
      if (argc != 2) {
         user_reply(cptr, "Usage: /user resetpw <user>");

         return;
      }
      char password[9];

      if (!user_temp_password(password, sizeof(password) ) ) {
         user_reply(cptr, "USER: unable to generate a temporary password");

         return;
      }
      char *password_hash = hash_passwd(password);
      bool ok = password_hash && db_user_update_password(masterdb, target->name, password_hash, true, now + 7 * 86400);
      free(password_hash);

      if (!ok || !user_reload_database(cptr) ) {
         user_reply(cptr, "USER: failed to reset password for %s", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s reset password for %s", cptr->chatname, target->name);
      user_reply(cptr, "USER: temporary password for %s: %s (password change required at next login)", target->name, password);

      return;
   }

   if (strcasecmp(argv[0], "pass") == 0) {
      if (argc != 3 || !argv[2] || strlen(argv[2]) < 8 || strlen(argv[2]) > 128) {
         user_reply(cptr, "Usage: /user pass <user> <password> (8-128 characters)");

         return;
      }
      char *password_hash = hash_passwd(argv[2]);
      bool ok = password_hash && db_user_update_password(masterdb, target->name, password_hash, false, 0);
      free(password_hash);

      if (!ok || !user_reload_database(cptr) ) {
         user_reply(cptr, "USER: failed to change password for %s", target->name);

         return;
      }
      Log(LOG_AUDIT, "auth.users", "%s changed password for %s", cptr->chatname, target->name);
      user_reply(cptr, "USER: password changed for %s", target->name);

      return;
   }

   user_reply(cptr, "USER: unknown subcommand %s", argv[0]);
#endif
}

void rrserver_register_events(void) {
   extern void rrserver_objects_register_events(void);
   rrserver_objects_register_events();
   rrserver_media_register_events();
   Log(LOG_CRAZY, "events", "Registering rrserver events");

   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("authdb.load", rrserver_handle_authdb_load, NULL);
   event_on("be.poll", rrserver_handle_be_poll, NULL);
   event_on("hello", rrserver_handle_hello, NULL);
   event_on("latency", rrserver_handle_latency, NULL);
   event_on("ptt.off", rrserver_handle_ptt_off, NULL);
   event_on("quota.cmd", rrserver_handle_quota_cmd, NULL);
   event_on("user.cmd", rrserver_handle_user_cmd, NULL);
   event_on("recording-start", rrserver_handle_recording_start, NULL);
   event_on("recording-stop", rrserver_handle_recording_stop, NULL);
   event_on("rehash", rrserver_handle_rehash, NULL);
   event_on("rig.ptt", rrserver_handle_rig_ptt_off, NULL);
   event_on("rigctl", rrserver_handle_rigctlmsg, NULL);
   event_on(RR_PROPERTY_CHANGED_EVENT, rrserver_handle_rig_property_changed, NULL);
   event_on("send-chat-replay", rrserver_handle_send_chat_replay, NULL);
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
   event_on("send-cat-state", rrserver_handle_send_cat_state, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
   Log(LOG_CRAZY, "events", "Finished registering rrserver events");
}

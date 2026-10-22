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
#include <glib.h>
#include <errno.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrserver/database.h>
#include <rrserver/files.h>
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

#include <rrserver/events.internal.h>

extern struct GlobalState rig;
extern void rrserver_media_register_events(void);  // media.c

/* Inspect an authenticated protocol hello using existing diagnostic logging. */
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

/* Log event payloads for protocol events without a matching handler. */
static void rrserver_handle_nomatch(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   Log(LOG_WARN, "ws.nomatch", "NOMATCH: %s", data);
}

/* cat.freq is an intentional notification from librrprotocol after a successful frequency change. The CAT broadcast and rigctl event already handle
 * distribution and application; acknowledge this optional event to avoid treating normal tuning as an unhandled protocol warning. */
static void rrserver_handle_cat_freq(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;
}

/* Resolve a replay request and send stored room history. */
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

/* Per-account token bucket. Excessive chat is temporarily shunned. */
typedef struct {
   double tokens;
   uint64_t updated, shunned_until, warned;
} rr_chat_flood_t;
static GHashTable *chat_flood_users;
static bool rr_chat_flood_blocked(rrconn_t *client, const char *room) {
   if (!client || !client->user) return true;
   if (has_priv(client->user->uid, "chat-flood")) return false;
#ifdef USE_SQLITE
   if (room && *room && db_resource_access_named(masterdb, client->user->name, "",
           "room", room, "chat-flood", time(NULL), room) == RR_RESOURCE_ALLOW)
      return false;
#endif
   if (!chat_flood_users)
      chat_flood_users = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
   gpointer key = GINT_TO_POINTER(client->user->uid + 1);
   rr_chat_flood_t *state = g_hash_table_lookup(chat_flood_users, key);
   int burst = cfg_get_int("chat.flood.burst", 5);
   int interval = cfg_get_int("chat.flood.interval-ms", 2000);
   int penalty = cfg_get_int("chat.flood.shun-seconds", 10);
   if (burst < 1) burst = 1;
   if (burst > 100) burst = 100;
   if (interval < 100) interval = 100;
   if (interval > 60000) interval = 60000;
   if (penalty < 1) penalty = 1;
   if (penalty > 3600) penalty = 3600;
   uint64_t stamp = mono_us();
   if (!state) {
      state = g_new0(rr_chat_flood_t, 1);
      state->tokens = burst;
      state->updated = stamp;
      g_hash_table_insert(chat_flood_users, key, state);
   }
   double elapsed = stamp >= state->updated ? (double)(stamp - state->updated) / 1000000.0 : 0;
   state->updated = stamp;
   state->tokens = MIN((double)burst, state->tokens + elapsed * 1000.0 / interval);
   if (stamp < state->shunned_until) return true;
   if (state->tokens >= 1.0) {
      state->tokens -= 1.0;
      return false;
   }
   state->shunned_until = stamp + (uint64_t)penalty * 1000000;
   if (stamp - state->warned > 1000000) {
      ws_send_notice(client, "Chat flood: messages blocked for %d seconds; slow down.", penalty);
      Log(LOG_WARN, "ws.chat", "Chat flood shun for %s (%ds)", client->chatname, penalty);
      state->warned = stamp;
   }
   return true;
}
/* Owner-authoritative, short-lived cluster presence. The incarnation changes on
 * server restart, while the sequence increases within that incarnation.
 * Neither metadata field is supplied by the client. */
static char typing_incarnation[37];
static unsigned long long typing_sequence;

static void rrserver_handle_typing(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   if (!data || !client || !client->authenticated) return;
   dict *d = json2dict(data);
   if (!d) return;
   const char *room = dict_get(d, "talk.room", "");
   const char *state = dict_get(d, "talk.typing", "");
   if (!*room || !ws_client_in_room(client, room) ||
       (strcmp(state, "started") && strcmp(state, "active") && strcmp(state, "paused") && strcmp(state, "idle"))) {
      dict_free(d);
      return;
   }
   if (!*typing_incarnation) {
      char *epoch = g_uuid_string_random();
      if (!epoch) {
         Log(LOG_WARN, "cluster.presence",
             "Cannot publish typing state in %s: unable to generate a server incarnation UUID", room);
         dict_free(d);
         return;
      }
      g_strlcpy(typing_incarnation, epoch, sizeof(typing_incarnation));
      g_free(epoch);
   }
   dict_add(d, "talk.from", client->chatname);
   dict_add(d, "talk.epoch", typing_incarnation);
   dict_add_ullong(d, "talk.seq", ++typing_sequence);
   dict_add_uint(d, "talk.ttl", strcmp(state, "idle") ? 10 : 0);
   /* No persistence or replay. Existing publish hook handles subscribed
    * branches; the room owner remains the only authority for this update. */
   ws_broadcast_room_dict(client, d, room);
   dict_free(d);
}

/* Persist and broadcast accepted room chat messages. */
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
   if ((!strcasecmp(msg_type, "pub") || !strcasecmp(msg_type, "action") ||
        !strcasecmp(msg_type, "priv") || !strcasecmp(msg_type, "privmsg")) &&
       rr_chat_flood_blocked(cptr, channel)) {
      dict_free(d);
      return;
   }

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

      if (!db_add_chat_msg(masterdb, now, cptr->chatname, target_name, "privmsg", dict_get(d, "talk.data", ""))) {
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
   if (strcasecmp(msg_type, "pub") == 0 || strcasecmp(msg_type, "action") == 0) {

      if (strcasecmp(msg_type, "action") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s * %s%s", channel ? channel : ws_authoritative_room(), cptr->chatname, dict_get(d, "talk.data", ""));
      } else if (strcasecmp(msg_type, "pub") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s <%s> %s", channel ? channel : ws_authoritative_room(), cptr->chatname, dict_get(d, "talk.data", ""));
      }

      const char *talk_from = dict_get(d, "talk.from", NULL);
      const char *talk_target = dict_get(d, "talk.target", NULL);
      const char *talk_msg_type = dict_get(d, "talk.msg_type", NULL);
      const char *talk_msg = dict_get(d, "talk.data", NULL);

#ifdef USE_SQLITE

      if (channel) {
         if (!db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, talk_msg)) {
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

/* Resolve a media channel and dispatch recording start/stop control. */
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
      Log(LOG_WARN, "record", "recording-%s for unknown media channel %s", start ? "start" : "stop", (uuid ? uuid : "<none>"));
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
   const char *recording_id = (!fwdsp_tx && start) ? rr_ptt_recording_id((rr_vfo_t) channel->vfo) : NULL;
   const char *who = fwdsp_tx ? "radio" : (talker && talker->ptt_vfo == 'A' + channel->vfo ? talker->chatname : NULL);

   if (start && (!who || !*who)) {
      Log(LOG_WARN, "record", "No transmitter for recording on channel %s", channel->uuid);
      dict_free(d);

      return true;
   }
   const char *record_file = (!fwdsp_tx && channel->vfo >= 0) ? rr_ptt_recording_file((rr_vfo_t) channel->vfo) : NULL;
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

/* Handle the recording-start event using shared channel resolution. */
static void rrserver_handle_recording_start(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   rrserver_recording_control(data, true);
}

/* Handle the recording-stop event using shared channel resolution. */
static void rrserver_handle_recording_stop(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   rrserver_recording_control(data, false);
}

/* Publish the requested current rig CAT state. */
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
   Log(LOG_AUDIT, "ptt", "Forced TX off (%s event)", (event ? event : "ptt.off"));
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
      Log(LOG_WARN, "rigctl", "Departing user %s held PTT but no VFO recorded; NOT touching rig TX", (who ? who : "(unknown)"));
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

/* Refresh synchronously before login and command authorization. */
static void rrserver_auth_refresh(const char *event, const void *data, size_t length, rrconn_t *client, void *context) {
#ifdef USE_SQLITE

   if (length == sizeof(rr_server_auth_refresh_t) && cfg_get_bool("net.http.authdb-dynamic", false)) {
      rr_server_auth_refresh_t *refresh = (rr_server_auth_refresh_t *) data;

      if (!db_refresh_users(masterdb)) {
         refresh->allowed = false;
      }
   }
#endif
}

/* Idle sessions also lose revoked permissions without waiting for a command. */
static void rrserver_auth_poll(const char *event, const char *data, rrconn_t *client, void *context) {
#ifdef USE_SQLITE
   static time_t checked;
   extern time_t now;

   if (checked != now && cfg_get_bool("net.http.authdb-dynamic", false)) {
      checked = now;
      db_refresh_users(masterdb);
   }
#endif
}

/*
 * A client (with admin/owner privs -- checked in srv.http.c) asked us to rehash: reload the config file and then the user database (which may come from the
 * sqlite users table or the http.users file depending on net.http.authdb-dynamic).
 */
static void rrserver_handle_rehash(const char *event, const char *data, rrconn_t *cptr, void *user) {
   Log(LOG_INFO, "core", "Rehashing server configuration (requested by %s)", (cptr && cptr->chatname[0] != '\0' ? cptr->chatname : "internal"));

   if (!cfg_reload(NULL)) {
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

/* Register server lifecycle listeners and each domain's protocol handlers. */
void rrserver_register_events(void) {
   extern void rrserver_objects_register_events(void);
   rrserver_objects_register_events();
   rrserver_files_register();
   rrserver_direct_register_events();
   rrserver_acl_register_events();
   rrserver_bridge_access_register_events();
   rrserver_websdr_register_events();
   rrserver_relay_register_events();
   rrserver_supervision_register();
   rrserver_media_register_events();
   rrserver_usage_register_events();
   rr_ptt_register_accounting_events();
   Log(LOG_CRAZY, "events", "Registering rrserver events");

   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("cat.freq", rrserver_handle_cat_freq, NULL);
   event_on("rehash", rrserver_handle_rehash, NULL);
   event_on("authdb.load", rrserver_handle_authdb_load, NULL);
   event_on_binary(RR_SERVER_AUTH_REFRESH_EVENT, rrserver_auth_refresh, NULL);
   event_on("server.poll", rrserver_auth_poll, NULL);
   event_on("hello", rrserver_handle_hello, NULL);
   event_on("latency", rrserver_handle_latency, NULL);
   event_on("ptt.off", rrserver_handle_ptt_off, NULL);
   event_on("recording-start", rrserver_handle_recording_start, NULL);
   event_on("recording-stop", rrserver_handle_recording_stop, NULL);
   event_on("rig.ptt", rrserver_handle_rig_ptt_off, NULL);
   event_on("send-chat-replay", rrserver_handle_send_chat_replay, NULL);
   event_on("send-cat-state", rrserver_handle_send_cat_state, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
   event_on("talk.typing", rrserver_handle_typing, NULL);
   rrserver_events_rooms_register();
   rrserver_events_rig_register();
   rrserver_events_quota_register();
   rrserver_events_users_register();
   rrserver_events_admin_register();
   Log(LOG_CRAZY, "events", "Finished registering rrserver events");
}

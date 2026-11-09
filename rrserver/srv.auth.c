//
// srv.auth.c: Server authentication stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/codecneg.h>
#include <librrprotocol/server.events.h>
#include <librustyaxe/transport.h>
#include <rrserver/database.h>

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef  HOST_POSIX
#ifndef INSTALL_PREFIX
#define WWW_ROOT_FALLBACK "./www"
#define WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else // HOST_POSIX
#define WWW_ROOT_FALLBACK "fs:www/"
#define WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

extern bool dying;
extern time_t now;
extern char session_token[HTTP_TOKEN_LEN + 1];
extern http_user_t http_users[HTTP_MAX_USERS];

static int generate_random_guest_id(int digits) {
   if (digits < 4) {
      return -1;
   }
   int num = 0, prev_digit = -1;

try_again:

   for (int i = 0 ; i < digits ; i++) {
      int digit;
      do {
         digit = rand() % 10;
      } while (digit == prev_digit);   // Ensure no consecutive repeats

      num = num * 10 + digit;
      prev_digit = digit;
   }

   rrconn_t *cptr = http_client_list;
   while (cptr) {
      // if we match an existing number, start over
      if (cptr->guest_id == num) {
         goto try_again;
      }
      cptr = cptr->next;
   }
   return num;
}

// find by login challenge
static rrconn_t *http_find_client_by_nonce(const char *nonce) {
   if (!nonce) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
      if (cptr->nonce[0] == '\0') {
         i++;
         cptr = cptr->next;
         continue;
      }

      if (strcmp(cptr->nonce, nonce) == 0) {
         Log(LOG_CRAZY, "http.core", "Found client at index %d by nonce", i);

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_CRAZY, "http.core", "No client matched nonce");

   return NULL;
}

bool match_priv(const char *user_privs, const char *priv) {
   if (user_privs == NULL || priv == NULL) {
      return false;
   }
   const char *start = user_privs;
   size_t privlen = strlen(priv);

   while (start && *start) {
      const char *end = strchr(start, ',');
      size_t len = end ? (size_t)(end - start) : strlen(start);

      char token[128];

      if (len >= sizeof(token)) {
         len = sizeof(token) - 1;
      }
      memcpy(token, start, len);
      token[len] = '\0';

      if (strcmp(token, priv) == 0) {
         return true;
      }

      // Serial account flags may restrict exports by a trailing name prefix.
      // Keep this extension scoped to serial rather than broadening other roles.
      if (len > 7 && !strncmp(token, "serial.", 7) && token[len - 1] == '*' && !strncmp(priv, token, len - 1)) {
         return true;
      }

      if (len >= 2 && token[len - 2] == '.' && token[len - 1] == '*') {
         token[len - 2] = '\0'; // strip .*

         if (strncmp(priv, token, strlen(token)) == 0 && priv[strlen(token)] == '.') {
            return true;
         }
      }
      start = end ? end + 1 : NULL;
   }
   return false;
}

bool has_priv(int uid, const char *priv) {
   if (priv == NULL || uid < 0 || (uid > HTTP_MAX_USERS - 1)) {
      return false;
   }
   const char *p = priv;
   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);

      char tmp[128];

      if (len >= sizeof(tmp)) {
         len = sizeof(tmp) - 1;
      }
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (http_users[uid].privs[0] == '\0') {
         return false;
      }

      if (match_priv(http_users[uid].privs, tmp)) {
         return true;
      }
      p = sep ? sep + 1 : NULL;
   }
   return false;
}

/* Bounded, process-local peer budget; reconnecting does not reset attempts. Count both challenge requests and replies, including invalid account names. */
static bool auth_peer_allowed(const char *ip) {
   static struct {
      char ip[64];
      time_t started;
      unsigned attempts;
   } peers[128];
   size_t slot = 0;

   for (size_t i = 0 ; i < sizeof(peers) / sizeof(peers[0]) ; i++) {
      if (!strcmp(peers[i].ip, ip ? ip : "unknown")) {
         slot = i;
         break;
      }

      if (peers[i].started < peers[slot].started) {
         slot = i;
      }
   }

   if (strcmp(peers[slot].ip, ip ? ip : "unknown") || now < peers[slot].started || now - peers[slot].started >= 60) {
      strlcpy(peers[slot].ip, ip ? ip : "unknown", sizeof(peers[slot].ip));
      peers[slot].started = now;
      peers[slot].attempts = 0;
   }

   return ++peers[slot].attempts <= 20;
}

///////////////////////////////////////
/* Persistence belongs to the application; absent listeners retain file-db behavior. */
bool ws_auth_refresh(rrconn_t *client) {
   rr_server_auth_refresh_t refresh = {
      .allowed = true
   };
   event_emit_binary(RR_SERVER_AUTH_REFRESH_EVENT, client, &refresh, sizeof(refresh));

   return refresh.allowed;
}

/* No password credential may travel over an unencrypted WebSocket. */
static bool auth_tls_connection(const rrconn_t *client) {
   if (!client) return false;
   return client->native_transport &&
      (rr_transport_capabilities(client->native_transport) & RR_TRANSPORT_TLS_ENCRYPTED);
}

static void auth_password_change_reply(rrconn_t *client, bool success, const char *reason) {
   dict *reply = dict_new();
   if (!reply) return;
   dict_add(reply, "msg.type", "auth");
   dict_add(reply, "auth.cmd", success ? "password-changed" : "password-change-failed");
   if (client && client->user) dict_add(reply, "auth.user", client->user->name);
   if (reason) dict_add(reply, "auth.reason", reason);
   if (client) ws_send_dict(NULL, client, reply, WEBSOCKET_OP_TEXT);
   dict_free(reply);
}

bool ws_handle_auth_msg(rrconn_t *cptr, dict *d) {
   bool rv = true;

   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got cptr:<%p> d:<%p>", cptr, d);

      return false;
   }

   if (!ws_auth_refresh(cptr)) {
      ws_send_error(cptr, "Account database unavailable; try again later");

      return false;
   }
   const char *cmd = dict_get(d, "auth.cmd", NULL);
   const char *pass = dict_get(d, "auth.pass", NULL);
   const char *token = dict_get(d, "auth.token", NULL);
   const char *user = dict_get(d, "auth.user", NULL);

   // Must always send a command and username during auth
   if (!cmd || (!user && !token)) {
      return false;
   }

   if (cptr->authenticated && (!strcasecmp(cmd, "login") || !strcasecmp(cmd, "pass"))) {
      ws_send_error(cptr, "Already authenticated; reconnect to change accounts");

      return false;
   }

   if ((!strcasecmp(cmd, "login") || !strcasecmp(cmd, "pass")) && !auth_tls_connection(cptr)) {
      ws_kick_client(cptr, "Password login requires TLS");
      return false;
   }

   if ((!strcasecmp(cmd, "login") || !strcasecmp(cmd, "pass")) && !auth_peer_allowed(cptr->user_ip)) {
      ws_kick_client(cptr, "Too many authentication attempts; try again in a minute");

      return false;
   }

   if (strcasecmp(cmd, "login") == 0) {
      if (!user || !*user) {
         Log(LOG_WARN, "auth", "Login request did not include a username");

         return false;
      }
      char resp_buf[HTTP_WS_MAX_MSG + 1];
      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      Log(LOG_AUDIT, "auth", "Login request from user %s on cptr:<%p> from %s:%d", user, cptr, ip, port);

      cptr->user = NULL;

      // search for user
      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         if (strcasecmp(http_users[i].name, user) == 0) {
            cptr->user = &http_users[i];
            break;
         }
      }

      // handle disabled accounts
      if (cptr->user == NULL) {
         Log(LOG_AUDIT, "auth.users", "No such account %s", user);
         ws_kick_client(cptr, "Invalid account/password");

         return false;
      }

      if (cptr->user->enabled == false) {
         Log(LOG_AUDIT, "auth.users", "User account %s is disabled", user);
         ws_kick_client(cptr, "Account disabled");

         return false;
      }

      if (!masterdb) {
         ws_kick_client(cptr, "SQLite authentication database unavailable");
         return false;
      }

      if (cptr->user->password_expires > 0 && cptr->user->password_expires <= now) {
         ws_kick_client(cptr, "Password expired; ask an administrator to reset it");

         return false;
      }

      int curr_clients = http_count_clients();

      if (curr_clients > HTTP_MAX_SESSIONS) {
         Log(LOG_AUDIT, "auth.users", "Server is full! %d clients exceeds max %d", curr_clients, HTTP_MAX_SESSIONS);
         // kick the user
         ws_kick_client(cptr, "Server full! Try again later.");

         return false;
      }

      if (cptr->user) {
         if (cptr->user->sessions + 1 > cptr->user->max_sessions) {
            Log(LOG_AUDIT, "auth.users", "User session limit reached for %s: %d sessions exceeds max %d", cptr->user->name, cptr->user->sessions, cptr->user->
               max_sessions);
            // Kick the client
            ws_kick_client(cptr, "Too many sessions");

            return false;
         }
      } else {
         Log(LOG_CRIT, "auth.users", "login request has no cptr->user for cptr:<%p>?!", cptr);
      }
      auth_generate_nonce(cptr->nonce, sizeof(cptr->nonce));
      dict *auth_msg = dict_new();
      dict_add(auth_msg, "msg.type", "auth");
      dict_add(auth_msg, "auth.cmd", "challenge");
      dict_add(auth_msg, "auth.method", "tls-password-v1");
      dict_add(auth_msg, "auth.nonce", cptr->nonce);
      dict_add(auth_msg, "auth.user", user);
      dict_add(auth_msg, "auth.token", cptr->token);
      Log(LOG_CRAZY, "auth", "Sending login challenge to cptr <%p>", cptr);
      ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
      dict_free(auth_msg);
   } else if (!strcasecmp(cmd, "change-password")) {
      /* Self-service password rotation over the existing verified TLS session.
       * Do not route credentials through chat commands or application logs. */
      const char *replacement = dict_get(d, "auth.new-password", NULL);
      if (!cptr->authenticated || !cptr->user || !auth_tls_connection(cptr) ||
          !token || strcmp(token, cptr->token) || !user ||
          strcasecmp(user, cptr->user->name) || !replacement ||
          strlen(replacement) < 8 || strlen(replacement) > 256) {
         auth_password_change_reply(cptr, false, "A verified TLS session and 8-256 character password are required");
         return false;
      }
      char *encoded = auth_password_hash_argon2id(replacement);
      bool changed = encoded && masterdb &&
         db_user_update_password(masterdb, cptr->user->name, encoded, false, 0);
      if (encoded) {
         explicit_bzero(encoded, strlen(encoded));
         free(encoded);
      }
      if (!changed || db_get_users(masterdb) < 0) {
         auth_password_change_reply(cptr, false, "Unable to persist password change");
         return false;
      }
      auth_password_change_reply(cptr, true, NULL);
      Log(LOG_AUDIT, "auth", "Self-service password changed for %s", cptr->user->name);
   } else if (strcasecmp(cmd, "logout") == 0 || strcasecmp(cmd, "quit") == 0) {
      Log(LOG_DEBUG, "auth", "Logout request from %s cptr:<%p>", (cptr && cptr->chatname[0] != '\0' ? cptr->chatname : ""), cptr);
      /* PARITY: rrclient/cmd.misc.c cmd_quit and rustyrig-www/js/webui.auth.js logout. */
      const char *reason = !strcasecmp(cmd, "quit") ? dict_get(d, "auth.msg", "Logged out. 73!") : "Logged out. 73!";
      ws_kick_client(cptr, reason && *reason ? reason : "Logged out. 73!");
   } else if (strcasecmp(cmd, "pass") == 0) {
      bool guest = false;

      if (pass == NULL || token == NULL) {
         Log(LOG_DEBUG, "auth", "auth pass command without password <%p> / token <%p>", pass, token);
         ws_kick_client(cptr, "auth.pass message incomplete/invalid. Goodbye");

         return false;
      }

      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      if (cptr->user == NULL) {
         Log(LOG_WARN, "auth", "cptr-> user == NULL handling conn from ip %s:%d, Kicking!", ip, port);
         ws_kick_client(cptr, "Invalid login/password");

         return false;
      }

      int login_uid = cptr->user->uid;

      if (login_uid < 0 || login_uid >= HTTP_MAX_USERS) {
         Log(LOG_WARN, "auth", "Invalid uid for username |%s| from IP %s:%d", cptr->chatname, ip, port);
         ws_kick_client(cptr, "Invalid login/passowrd");

         return false;
      }

      http_user_t *up = &http_users[login_uid];

      if (up == NULL) {
         Log(LOG_WARN, "auth", "Uid %d returned NULL http_user_t", login_uid);

         return false;
      }

      if (!up->enabled || (up->password_expires > 0 && up->password_expires <= now) || strcmp(token, cptr->token) || (user && strcasecmp(user, up->name))) {
         ws_kick_client(cptr, "Invalid or expired login challenge");

         return false;
      }

      const char *method = dict_get(d, "auth.method", "");
      if (strcmp(method, "tls-password-v1") || strlen(pass) > 256 ||
          !cptr->nonce[0] || !auth_tls_connection(cptr)) {
         cptr->nonce[0] = '\0';
         ws_kick_client(cptr, "Unsupported or expired TLS password challenge");
         return false;
      }
      /* Consume the nonce before expensive verification. */
      cptr->nonce[0] = '\0';
      bool upgraded = false;
      bool password_valid = db_user_password_authenticate(masterdb, up->name, pass, &upgraded);
      if (upgraded)
         Log(LOG_INFO, "auth", "Upgraded legacy account %s to Argon2id", up->name);

      if (password_valid) {
         /* Never grant authenticated access on an unresolved ACL decision.
          * The listener event is backend-independent. */
         rr_server_admission_t admission = {
            auth_tls_connection(cptr) ? "https" : "http", cptr->user_ip, up, false
         };
         event_emit_binary(RR_SERVER_ADMISSION_EVENT, cptr, &admission, sizeof(admission));

         if (!admission.allowed) {
            ws_kick_client(cptr, "Listener access denied");
            return false;
         }

         // special handling for guests; we generate a random suffix
         // force rewriting if they use any nick starting with Guest.
         if (strncasecmp(up->name, "guest", 5) == 0) {
            cptr->guest_id = generate_random_guest_id(4);
            memset(cptr->chatname, 0, sizeof(cptr->chatname));
            snprintf(cptr->chatname, sizeof(cptr->chatname), "GUEST%04d", cptr->guest_id);
            guest = true;
         } else {
            prepare_msg(cptr->chatname, sizeof(cptr->chatname), "%s", up->name);
         }
         cptr->authenticated = true;
         cptr->user->sessions++;

         Log(LOG_AUDIT, "auth", "Verified credentials for %s", up->name);


         // Store some timestamps such as when user joined & session will
         // forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;

         // XXX: This might be a bad idea
//         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;
         ////////////////////
         // Set user flags //
         ////////////////////
         if (has_priv(cptr->user->uid, "admin|owner")) {
            client_set_flag(cptr, FLAG_SYSLOG);
         }

         if (has_priv(cptr->user->uid, "admin|owner")) {
            client_set_flag(cptr, FLAG_STAFF);
         }

         if (has_priv(cptr->user->uid, "tx")) {
            client_set_flag(cptr, FLAG_CAN_TX);
         }

         // Media source connections (rrmedia, fwdsp feeds, remote relays)
         // may push binframes for the channels they subscribe to
         if (has_priv(cptr->user->uid, "media.source")) {
            client_set_flag(cptr, FLAG_MEDIA_SOURCE);
         }

         // client cannot transmit unless a user with elmer flag is logged in
         if (has_priv(cptr->user->uid, "noob")) {
            client_set_flag(cptr, FLAG_NOOB);
         }

         // client is an elmer and can allow noobs to control rig
         if (has_priv(cptr->user->uid, "elmer")) {
            client_set_flag(cptr, FLAG_ELMER);
         }
         // Send a ping to the user and expect them to reply within
         // HTTP_PING_TIMEOUT seconds
         cptr->last_heard = now;

         // Send last message (AUTHORIZED) of the login sequence to let client
         // know they are logged in
         dict *auth_msg = dict_new();
         dict_add(auth_msg, "msg.type", "auth");
         dict_add(auth_msg, "auth.cmd", "authorized");
         dict_add(auth_msg, "auth.privs", cptr->user->privs);
         dict_add(auth_msg, "auth.token", token);
         dict_add(auth_msg, "auth.user", cptr->chatname);
         dict_add_bool(auth_msg, "auth.password-change-required", cptr->user->password_change_required);
         dict_add_ulong(auth_msg, "auth.password-set", (unsigned long)cptr->user->password_set);
         dict_add_ulong(auth_msg, "auth.password-expires", (unsigned long)cptr->user->password_expires);
         dict_add_ulong(auth_msg, "auth.ts", now);
         ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
         dict_free(auth_msg);
         auth_msg = NULL;

         // Push the current rig state to this freshly-authenticated client so
         // their UI populates immediately instead of waiting for the next
         // announce interval to elapse.
         // NB: event_emit_dict() silently drops the event when data is NULL,
         // so pass an empty payload; the handler doesn't read it.
         event_emit("send-cat-state", cptr, "");

         // Don't send a ping here: the client is still busy syncing (UI setup,
         // chat replay, media negotiation) and can't echo the pong promptly,
         // which would report a bogus huge RTT. The session housekeeping loop
         // (http_expire_sessions) sends the first ping HTTP_PING_TIME seconds
         // after last_heard, i.e. once the connection has settled.

         Log(LOG_AUDIT, "auth", "User %s on cptr <%p> logged in from IP %s:%d (session #%d/%d) with privs: %s (client: %s, ua: %s)", cptr->chatname, cptr, cptr
            ->user_ip, cptr->user_port, cptr->user->sessions, cptr->user->max_sessions, cptr->user->privs, (cptr->cli_version ? cptr->cli_version : "unknown"),
            (cptr->user_agent ? cptr->user_agent : "unknown"));

         // Send our capabilities
         const char *configured_codecs = cfg_get_exp("codecs.allowed");
         char *my_codecs = codec_filter_test_mode(configured_codecs, cfg_get_bool("audio.test-mode", true));
         dict *capab_msg = media_capab_prepare(my_codecs);
         free((void *)configured_codecs);
         free((void *)my_codecs);

         if (capab_msg) {
            ws_send_dict(NULL, cptr, capab_msg, WEBSOCKET_OP_TEXT);
            dict_free(capab_msg);
         } else {
            Log(LOG_CRIT, "ws.media", ">> No codecs negotiated");
         }
         // Every authenticated session joins the site lobby; rig rooms are opt-in.
         ws_client_join_room(cptr, ws_site_room());
         // Tell the client which media channels (RX/TX audio per VFO, etc)
         // exist so it can subscribe; the event is handled by the program's
         // media module which owns the channel registry.
         event_emit("send-media-channels", cptr, "");
         /////////////////////
         // XXX: We should move this out to it's own function like
         dict *talk_msg = dict_new();
         dict_add(talk_msg, "msg.type", "talk");
         dict_add_ulong(talk_msg, "msg.ts", now);
         dict_add(talk_msg, "talk.cmd", "join");
         dict_add(talk_msg, "talk.ip", ip);
         dict_add(talk_msg, "talk.muted", (cptr->user->is_muted ? "true" : "false"));
         dict_add(talk_msg, "talk.privs", cptr->user->privs);
         dict_add(talk_msg, "talk.target", ws_site_room());
         dict_add(talk_msg, "talk.room", ws_site_room());
         dict_add_bool(talk_msg, "room.site", true);
         dict_add_bool(talk_msg, "room.has-vfos", ws_room_has_vfos(ws_site_room()));
         dict_add_ulong(talk_msg, "room.vfo-mask", ws_room_vfo_mask(ws_site_room()));
         dict_add(talk_msg, "talk.user", cptr->chatname);
         dict_add_int(talk_msg, "talk.sessions", cptr->user->sessions);
         ws_broadcast_room_dict(NULL, talk_msg, ws_site_room());
         event_emit_dict("room.join", cptr, talk_msg);
         ws_send_users(NULL);
         dict_free(talk_msg);
         talk_msg = dict_new();
         dict_add(talk_msg, "msg.type", "talk");
         dict_add_ulong(talk_msg, "msg.ts", now);
         dict_add(talk_msg, "talk.target", ws_site_room());
         dict_add(talk_msg, "talk.room", ws_site_room());
         dict_add_bool(talk_msg, "room.site", true);
         dict_add_bool(talk_msg, "room.has-vfos", ws_room_has_vfos(ws_site_room()));
         dict_add_ulong(talk_msg, "room.vfo-mask", ws_room_vfo_mask(ws_site_room()));
         dict_add(talk_msg, "talk.user", cptr->chatname);
         event_emit_dict("send-chat-replay", cptr, talk_msg);
         dict_free(talk_msg);
      } else {
         Log(LOG_AUDIT, "auth", "User %s on cptr <%p> from IP %s:%d gave wrong password. Kicking!", cptr->user->name, cptr, ip, port);
         ws_kick_client(cptr, "Invalid login/password");
         rv = false;
      }

      /* auth.pass belongs to the incoming dictionary: never log credentials. */
   }
cleanup:

   return rv;
}

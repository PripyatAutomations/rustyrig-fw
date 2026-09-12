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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/ptt.h>


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

   Log(LOG_CRIT, "ws.rigctl", "cmd: %s, vfo: %s, from: %s, freq: %d, mode: %s, width: %s",
     rc_cmd, rc_vfo, rc_from, rc_freq, rc_mode ? rc_mode : "(none)", rc_width ? rc_width : "(none)");

   if (!rc_cmd || !rc_vfo || !rc_from) {
      dict_free(d);
      return;
   }

   rr_vfo_t vfo = vfo_lookup(rc_vfo[0]);

   if (strcasecmp(rc_cmd, "ptt") == 0) {
      // Key/dekey the rig (from the PTT button in the client)
      Log(LOG_AUDIT, "rigctl", "User %s set PTT to %s on vfo %s", rc_from, (rc_ptt ? "true" : "false"), rc_vfo);
      rr_ptt_set(vfo, rc_ptt);
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
      rr_set_mode(vfo, new_mode);
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
      rr_set_width(vfo, rc_width);
      dict_free(d);
      return;
   }

   if (strcasecmp(rc_cmd, "power") == 0) {
      // Set the rig power (from !power chat command or ws cat.cmd power)
      if (rc_power <= 0) {
         Log(LOG_WARN, "ws.rigctl", "POWER set with bogus value %f", rc_power);
         dict_free(d);
         return;
      }

      // Audit trail: who changed which VFO to what power
      Log(LOG_AUDIT, "rigctl", "User %s set VFO %s POWER to %f watts", rc_from, rc_vfo, rc_power);
      rr_set_power(vfo, rc_power);
      dict_free(d);
      return;
   }

   fprintf(stderr, "setting vfo %s freq to %d\n", rc_vfo, rc_freq);

   // Audit trail: who changed which VFO to what frequency
   Log(LOG_AUDIT, "rigctl", "User %s set VFO %s FREQ to %d hz", rc_from, rc_vfo, rc_freq);

   rr_freq_set(vfo, rc_freq);
   dict_free(d);
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
   if (channel) {
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
   Log(LOG_CRAZY, "events.ws", "ENTER talk.msg: event=<%s> data=<%p> cptr=<%p>",
      event ? event : "(null)", data, cptr);

   if (!data || !cptr) {
      Log(LOG_CRIT, "ws.chat", "handle_talkmsg with data:<%p> and cptr:<%p>",
         data, cptr);
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
    * File chunks aren't normal chat messages and should not be
    * written to chat_log. They still need to be broadcast.
    */
   if (strcasecmp(msg_type, "file_chunk") == 0) {
      Log(LOG_DEBUG, "ws.chat", "broadcasting file chunk from %s", cptr->chatname);
      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
      return;
   }

   /*
    * Normal public/action messages.
    */
   if (strcasecmp(msg_type, "pub") == 0 ||
       strcasecmp(msg_type, "action") == 0) {

      if (strcasecmp(msg_type, "action") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s * %s%s",
            channel ? channel : "&localrig",
            cptr->chatname,
            dict_get(d, "talk.data", ""));
      } else if (strcasecmp(msg_type, "pub") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s <%s> %s",
            channel ? channel : "&localrig",
            cptr->chatname, dict_get(d, "talk.data", ""));
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

      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcasting: from=<%s> target=<%s> type=<%s>",
         talk_from, talk_target, talk_msg_type);

      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcast returned");
      dict_free(d);
      return;
   }

   Log(LOG_DEBUG, "ws.chat", "unknown talk.msg type: %s", msg_type);
   dict_free(d);
}


static void rrserver_handle_recording_start(const char *event,
                                            const char *data,
                                            rrconn_t *cptr,
                                            void *user) {
   // Deal with this
}


static void rrserver_handle_recording_stop(const char *event,
                                           const char *data,
                                           rrconn_t *cptr,
                                           void *user) {
   // Deal with this
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
   rr_ptt_set_all_off();
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
      rr_ptt_set(vfo_lookup(vfo[0]), false);
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
 * librrprotocol emits "authdb.load" when net.http.authdb-dynamic is true
 * (see srv.auth.passdb.c: http_reload_users()); we fill http_users[] from
 * the sqlite users table here.
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
 * A client (with admin/owner privs -- checked in srv.http.c) asked us to
 * rehash: reload the config file and then the user database (which may come
 * from the sqlite users table or the http.users file depending on
 * net.http.authdb-dynamic).
 */
static void rrserver_handle_rehash(const char *event, const char *data, rrconn_t *cptr, void *user) {
   Log(LOG_INFO, "core", "Rehashing server configuration (requested by %s)",
      (cptr && cptr->chatname[0] != '\0' ? cptr->chatname : "internal"));

   if (cfg_reload(NULL) ) {
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
   quota_reply((rrconn_t *)user, "  %-16s %s", name, time_t2dhms(credits));
   return 1;
}

static void quota_apply(rrconn_t *cptr, const char *actor, const char *subcmd, int argc, char **argv) {
   if (!masterdb || !subcmd) {
      return;
   }

   if (strcasecmp(subcmd, "LIST") == 0) {
      quota_reply(cptr, "TX quotas (minutes remaining):");
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

            quota_reply(cptr, "%s: %d minutes (%d seconds) remaining", name, mins, (before < 0 ? 0 : before));

         } else {
            if (db_quota_set(masterdb, name, 60 * 60) ) {
               Log(LOG_AUDIT, "quota", "%s reset %s's TX quota to 60 minutes (was %d)",
                  actor, name, (before < 0 ? 0 : before) / 60);
               quota_reply(cptr, "%s: reset to 60 minutes", name);
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
               char *was = time_t2dhms((time_t)(before < 0 ? 0 : before));
               char *added = time_t2dhms(secs);
               char *now = time_t2dhms((time_t)db_quota_get(masterdb, name));
               Log(LOG_AUDIT, "quota", "%s added %s to %s's TX quota (was %s)", actor, added, name, was);
               quota_reply(cptr, "added %s (now %s) to %s's quota", added, now, name);
               free( (void *)was);
               free( (void *)added);
               free( (void *)now);
            }
         } else {
            ok = db_quota_set(masterdb, name, (int)secs);

            if (ok) {
               char *was = time_t2dhms((time_t)(before < 0 ? 0 : before));
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

void rrserver_register_events(void) {
   extern void rrserver_media_register_events(void);   // media.c
   rrserver_media_register_events();
   Log(LOG_CRAZY, "events", "Registering rrserver events");

   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("authdb.load", rrserver_handle_authdb_load, NULL);
   event_on("be.poll", rrserver_handle_be_poll, NULL);
   event_on("hello", rrserver_handle_hello, NULL);
   event_on("latency", rrserver_handle_latency, NULL);
   event_on("ptt.off", rrserver_handle_ptt_off, NULL);
   event_on("quota.cmd", rrserver_handle_quota_cmd, NULL);
   event_on("recording-start", rrserver_handle_recording_start, NULL);
   event_on("recording-stop",rrserver_handle_recording_stop, NULL);
   event_on("rehash", rrserver_handle_rehash, NULL);
   event_on("rig.ptt", rrserver_handle_rig_ptt_off, NULL);
   event_on("rigctl", rrserver_handle_rigctlmsg, NULL);
   event_on("send-chat-replay", rrserver_handle_send_chat_replay, NULL);
   event_on("send-cat-state", rrserver_handle_send_cat_state, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
   Log(LOG_CRAZY, "events", "Finished registering rrserver events");
}

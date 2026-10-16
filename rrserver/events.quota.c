//
// rrserver/events.quota.c: quota event listeners from
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

/*
 * A client (with admin/owner privs -- checked in srv.chat.c) ran /quota:
 *   LIST | SHOW <user>... | ADD <user> <minutes> | RESET <user>... |
 *   SET <user> <minutes>
 * Replies to the requesting client via ws_send_notice().
 */

// db_quota_list callback: print one row to the requesting client
static int quota_list_cb(const char *name, int credits, void *user) {
   ws_send_notice((rrconn_t *) user, "  %-16s %s", name, time_t2dhms(credits));

   return 1;
}

/* Apply an authorized legacy TX-credit operation and report its result. */
static void quota_apply(rrconn_t *cptr, const char *actor, const char *subcmd, int argc, char **argv) {
   if (!masterdb || !subcmd) {
      return;
   }

   if (strcasecmp(subcmd, "LIST") == 0) {
      ws_send_notice(cptr, "TX quotas (time remaining):");
      db_quota_list(masterdb, quota_list_cb, cptr);

      return;
   }

   if (strcasecmp(subcmd, "HELP") == 0) {
      ws_send_notice(cptr, "Usage: /quota [TX|BW] LIST | SHOW <user>... | ADD <user> <amount> | RESET <user>... | SET <user> <amount>");
      ws_send_notice(cptr, "  ADD/SET take a dhms time string like 30m, 2h, 1d or 1w2d (0 = no TX allowed);");
      ws_send_notice(cptr, "  BW amounts: 10=10M bytes; whole decimal M/G/T/P units. BW RESET clears usage and restores quota.bandwidth.default.");
      ws_send_notice(cptr, "  SHOW shows exact seconds too.");

      return;
   }

   if (strcasecmp(subcmd, "SHOW") == 0 || strcasecmp(subcmd, "RESET") == 0) {
      if (argc < 1) {
         ws_send_notice(cptr, "quota %s: no user given", subcmd);

         return;
      }

      for (int i = 0 ; i < argc ; i++) {
         const char *name = argv[i];
         int before = db_quota_get(masterdb, name);

         if (strcasecmp(subcmd, "SHOW") == 0) {
            int mins = (before < 0 ? 0 : before) / 60;

            struct rr_usage usage;

            if (db_usage_get(masterdb, name, &usage)) {
               ws_send_notice(cptr, "%s: %s remaining; TX used=%llu seconds since reset", name, time_t2dhms(before), (unsigned long long)usage.tx_seconds);
            } else {
               ws_send_notice(cptr, "%s: %s remaining", name, time_t2dhms(before));
            }
         } else {
            bool saved = sqlite3_exec(masterdb, "SAVEPOINT quota_tx_reset", NULL, NULL, NULL) == SQLITE_OK;

            if (saved) {
               saved = db_quota_set(masterdb, name, 60 * 60) && db_usage_reset_tx(masterdb, name);

               if (!saved) {
                  sqlite3_exec(masterdb, "ROLLBACK TO quota_tx_reset", NULL, NULL, NULL);
               }
               saved = sqlite3_exec(masterdb, "RELEASE quota_tx_reset", NULL, NULL, NULL) == SQLITE_OK && saved;
            }

            if (saved) {
               Log(LOG_AUDIT, "quota", "%s reset %s's TX quota to 60m (was %s)", actor, name, time_t2dhms(before));
               ws_send_notice(cptr, "%s: reset to 60m", name);
               quota_reset_warned(name);
            } else {
               event_emit("accounting.write.failed", NULL, "TX quota reset transaction");
               ws_send_notice(cptr, "quota RESET: failed for %s", name);
            }
         }
      }

      return;
   }

   if (strcasecmp(subcmd, "ADD") == 0 || strcasecmp(subcmd, "SET") == 0) {
      if (argc < 2) {
         ws_send_notice(cptr, "Usage: /quota %s <user> <time> [user2 time2 ...] (time like 30m, 2h, 1d)", subcmd);

         return;
      }

      // Pairs: user time [user time ...]; time is a dhms string (see
      // librustyaxe/util.time.c dhms2time_t), e.g. 90m, 2h, 1d, 1w2d
      for (int i = 0 ; i + 1 < argc ; i += 2) {
         const char *name = argv[i];
         time_t secs = dhms2time_t(argv[i + 1]);
         int before = db_quota_get(masterdb, name);

         if (secs <= 0 && argv[i + 1][0] != '0') {
            ws_send_notice(cptr, "quota %s: invalid time '%s' for %s (try 30m, 2h, 1d)", subcmd, argv[i + 1], name);

            return;
         }

         if (secs <= 0 && strcasecmp(subcmd, "ADD") == 0) {
            // ADD 0 is a no-op (would just leave credits unchanged)
            ws_send_notice(cptr, "quota ADD: nothing to add for %s (got 0)", name);
            continue;
         }
         bool ok;

         if (strcasecmp(subcmd, "ADD") == 0) {
            ok = db_quota_add(masterdb, name, (int)secs);

            if (ok) {
               char *was = time_t2dhms((time_t) (before < 0 ? 0 : before));
               char *added = time_t2dhms(secs);
               char *now = time_t2dhms((time_t) db_quota_get(masterdb, name));
               Log(LOG_AUDIT, "quota", "%s added %s to %s's TX quota (was %s)", actor, added, name, was);
               ws_send_notice(cptr, "added %s (now %s) to %s's quota", added, now, name);
               free((void *)was);
               free((void *)added);
               free((void *)now);
            }
         } else {
            ok = db_quota_set(masterdb, name, (int)secs);

            if (ok) {
               char *was = time_t2dhms((time_t) (before < 0 ? 0 : before));
               char *set = time_t2dhms(secs);
               Log(LOG_AUDIT, "quota", "%s set %s's TX quota to %s (was %s)", actor, name, set, was);
               ws_send_notice(cptr, "%s set %s's quota to %s", actor, name, set);
               free((void *)was);
               free((void *)set);
            }
         }

         if (!ok) {
            ws_send_notice(cptr, "quota %s: failed for %s", subcmd, name);
         } else {
            quota_reset_warned(name);
         }
      }

      if (argc % 2 != 0) {
         ws_send_notice(cptr, "quota %s: dangling argument '%s' (expected user time pairs)", subcmd, argv[argc - 1]);
      }

      return;
   }

   ws_send_notice(cptr, "Unknown quota subcommand: %s (try LIST, SHOW, ADD, RESET, SET)", subcmd);
}

/* Parse quota operations and expand supported user wildcard patterns. */
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
         strcasecmp(target, "SET") == 0 || strcasecmp(target, "HELP") == 0 || !strcasecmp(target, "BW") || !strcasecmp(target, "TX"));

   if (target_is_cmd) {
      snprintf(tail, sizeof(tail), "%s %s", target, data_str ? data_str : "");
   } else if ((!data_str || data_str[0] == '\0') && target) {
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
      rrserver_usage_quota(cptr, "LIST", 0, NULL);
      dict_free(d);

      return;
   }

   bool explicit_unit = !strcasecmp(argv[0], "BW") || !strcasecmp(argv[0], "TX");
   bool bandwidth = explicit_unit && !strcasecmp(argv[0], "BW");
   const char *action = explicit_unit ? (argc > 1 ? argv[1] : "LIST") : argv[0];
   int start = explicit_unit ? 2 : 1;
   bool list = !strcasecmp(action, "LIST"), show = !strcasecmp(action, "SHOW"), reset = !strcasecmp(action, "RESET");
   bool pairs = !strcasecmp(action, "SET") || !strcasecmp(action, "ADD");

   if ((!list && !show && !reset && !pairs) || (!list && argc <= start) || (pairs && (argc - start) % 2)) {
      if (bandwidth) {
         rrserver_usage_quota(cptr, action, argc > start ? argc - start : 0, &argv[start]);
      } else {
         quota_apply(cptr, cptr->chatname, action, argc > start ? argc - start : 0, &argv[start]);
      }
      dict_free(d);

      return;
   }
   /* Expand against known accounts in memory, then bind literal usernames. Overlapping patterns affect each account at most once per command. */
   bool seen[HTTP_MAX_USERS] = {
      false
   };
   int stop = argc > start ? argc : start + 1;

   for (int i = start ; i < stop ; i += pairs ? 2 : 1) {
      const char *pattern = i < argc ? argv[i] : "*";
      bool matched = false;

      for (unsigned uid = 0 ; uid < HTTP_MAX_USERS ; uid++) {
         const char *name = http_users[uid].name;

         if (!*name || !rrserver_quota_matches(pattern, name)) {
            continue;
         }
         matched = true;

         if (seen[uid]) {
            continue;
         }
         seen[uid] = true;
         char *single[] = {
            (char *)name, pairs ? argv[i + 1] : NULL
         };

         if (bandwidth || (list && !explicit_unit)) {
            rrserver_usage_quota(cptr, list ? "SHOW" : action, pairs ? 2 : 1, single);
         } else {
            quota_apply(cptr, cptr->chatname, list ? "SHOW" : action, pairs ? 2 : 1, single);
         }
      }

      if (!matched) {
         ws_send_error(cptr, "quota: no users match %s (simple * and ? wildcards only)", pattern);
      }
   }

   dict_free(d);
}

/* Register this domain's server-owned protocol listeners. */
void rrserver_events_quota_register(void) {
   event_on("quota.cmd", rrserver_handle_quota_cmd, NULL);
}

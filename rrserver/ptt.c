//
// rrserver/ptt.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
/*
 * Handle PTT and all interlocks preventing it's use
 *
 * we also deal with the PA_INHIBIT lines which allow momentarily stopping RF output without powering down the PAs (such as for relay changes in tuning or
 * filters).
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <rrserver/au.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <librrprotocol/objects.h>
#include <rrserver/rig.registry.h>
#include <rrserver/relay.h>
#include <rrserver/ptt.h>
#include <rrserver/supervision.h>
#include <rrserver/media.h>
#include <rrserver/recording.h>
#include <rrserver/rig.properties.h>
#include <rrserver/timer.h>
#ifdef  USE_SQLITE
#include <rrserver/database.h>
#endif

extern struct GlobalState rig;  // Global state
extern time_t ptt_tot_time;

time_t global_tot_time = 0;     // TOT
int vfos_enabled = 2;           // A + B by default

// VFO currently keyed by PTT logging (row id in ptt_log, -1 none)
static int ptt_log_session[MAX_VFOS];
static rrconn_t *ptt_log_owner[MAX_VFOS];
static char ptt_grant_ticket[MAX_VFOS][33];
static char ptt_recording_id[MAX_VFOS][RECORDING_ID_BUFSIZE];
static char ptt_recording_file[MAX_VFOS][FWDSP_RECORD_FILE_LEN];

const char *rr_ptt_recording_id(rr_vfo_t vfo) {
   if (vfo < VFO_A || vfo >= MAX_VFOS || !ptt_recording_id[vfo][0]) {
      return NULL;
   }

   return ptt_recording_id[vfo];
}

const char *rr_ptt_recording_file(rr_vfo_t vfo) {
   if (vfo < VFO_A || vfo >= MAX_VFOS || !ptt_recording_file[vfo][0]) {
      return NULL;
   }

   return ptt_recording_file[vfo];
}

static void ptt_filename_part(char *dst, size_t dst_len, const char *src) {
   if (!dst || dst_len == 0) {
      return;
   }
   snprintf(dst, dst_len, "%s", src && *src ? src : "unknown");

   for (char *p = dst ; *p ; p++) {
      if (!(('a' <= *p && *p <= 'z') || ('A' <= *p && *p <= 'Z') || ('0' <= *p && *p <= '9') || *p == '-' || *p == '_')) {
         *p = '_';
      }
   }
}

static bool ptt_prepare_recording_file(rr_vfo_t vfo, const char *username, const char *recording_id) {
   if (vfo < VFO_A || vfo >= MAX_VFOS || !recording_id || !*recording_id) {
      return false;
   }

   if (!rrserver_recording_enabled(true)) {
      return false;
   }
   char *record_dir = cfg_get_path("fwdsp:recording.path");

   if (!record_dir || !*record_dir) {
      free(record_dir);
      record_dir = cfg_get_path("path.record-dir");
   }

   if (!record_dir || !*record_dir) {
      free(record_dir);

      return false;
   }
   rr_mode_t mode = MODE_NONE;

   if (rig.ptt_rig) {
      char alias[] = {
         (char)('A' + vfo), 0
      };
      rr_server_vfo_t *object = rr_server_vfo_find_alias(rig.ptt_rig, alias);
      rr_property_snapshot_t state;

      if (object && rr_vfo_property_read(object, RR_PROP_VFO_MODE, &state) && state.known && state.available) {
         mode = vfo_parse_mode(state.value.s);
      }
   } else {
      mode = rr_get_mode(vfo);
   }
   const char *extension = rrserver_recording_codec(mode);
   time_t timestamp = time(NULL);
   struct tm local_time;
   char stamp[32];
   char safe_user[64];
   char safe_id[RECORDING_ID_BUFSIZE];
   localtime_r(&timestamp, &local_time);
   strftime(stamp, sizeof(stamp), "%Y%m%d.%H%M%S", &local_time);
   ptt_filename_part(safe_user, sizeof(safe_user), username);
   ptt_filename_part(safe_id, sizeof(safe_id), recording_id);
   int written =
      snprintf(ptt_recording_file[vfo], sizeof(ptt_recording_file[vfo]), "%s/%s.%s.%s.%s.%s", record_dir, stamp, safe_id, safe_user, "tx", extension);
   free(record_dir);

   if (written < 0 || (size_t)written >= sizeof(ptt_recording_file[vfo])) {
      ptt_recording_file[vfo][0] = '\0';
      Log(LOG_WARN, "ptt", "PTT recording filename is too long");

      return false;
   }

   return true;
}

// Set once when a user's remaining credits fall to/below quota.warning so we
// only nag them a single time until they're re-credited above the threshold
static bool quota_warned[HTTP_MAX_USERS];

// Clear the one-time low-credits latch for a user (after a top-up etc)
void quota_reset_warned(const char *username) {
   int uid = http_getuid(username);

   if (uid >= 0 && uid < HTTP_MAX_USERS) {
      quota_warned[uid] = false;
   }
}

#ifdef USE_SQLITE
static void quota_maybe_warn(rrconn_t *talker, int left) {
   if (!talker || !talker->user || talker->user->uid < 0 || talker->user->uid >= HTTP_MAX_USERS) {
      return;
   }

   int uid = talker->user->uid;
   int warning = cfg_get_int("quota.warning", 5) * 60;   // minutes remaining

   if (left > warning || left <= 0) {
      if (left <= 0) {
         quota_warned[uid] = false; // reset so they get re-warned after a top-up
      }

      return;
   }

   if (quota_warned[uid]) {
      return;                   // only once per crossing
   }
   quota_warned[uid] = true;

   char msg[256];

   snprintf(msg, sizeof(msg), "Low TX credits: %d minutes of TX remaining for %s", left / 60, talker->chatname);
   Log(LOG_WARN, "ptt", "TX quota: %s low credits: %d remaining", talker->chatname, left);

   if (!db_send_notice(talker, "privmsg", msg)) {
      Log(LOG_WARN, "ptt", "TX quota: failed to send low-credits notice to %s", talker->chatname);
   }
}

// Fetch the username recorded in a ptt_log row. Returns a malloc'd copy the
// caller must free, or NULL. Used at key-up when the talker is unknown (e.g.
// is_ptt was already cleared before the key-up reached us).
static char *ptt_log_username(int session) {
   if (!masterdb || session <= 0) {
      return NULL;
   }
   const char *sql = "SELECT username FROM ptt_log WHERE id = ?;";
   sqlite3_stmt *stmt;
   char *ret = NULL;

   if (sqlite3_prepare_v2(masterdb, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "ptt", "PTT log: failed to look up session %d: %s", session, sqlite3_errmsg(masterdb));

      return NULL;
   }
   sqlite3_bind_int(stmt, 1, session);

   if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0)) {
      ret = strdup((const char *)sqlite3_column_text(stmt, 0));
   }
   sqlite3_finalize(stmt);

   return ret;
}

#endif // USE_SQLITE

// Snapshot the VFO state and open a ptt_log row for the talker
static void ptt_log_start(rrconn_t *talker, rr_vfo_t vfo, const char *recording_id) {
#ifdef  USE_SQLITE

   if (!talker || !masterdb || vfo < 0 || vfo >= MAX_VFOS) {
      return;
   }

   if (ptt_log_session[vfo] > 0) {
      return;                   // already logging this VFO
   }

   float power = rr_get_power(vfo);
   // Read state from the vfos[] table (polled from the active backend), not
   // the hamlib cache: other backends (internal) don't maintain hl_state.
   rr_server_rig_t *radio = rig.ptt_rig ? rig.ptt_rig : rr_rig_registry_default(rig.rigs);
   const char *alias = rr_rig_registry_public_alias(rig.rigs, radio);
   const char *site = cfg_get("station.name");
   char scope[256];
   snprintf(scope, sizeof(scope), "%s.%s.vfo-%c", site && *site ? site : "unknown", alias ? alias : "unknown-rig", 'a' + vfo);
   struct rr_supervision_attribution consent;
   rrserver_supervision_snapshot(talker, talker->ptt_room, &consent);
   strlcpy(ptt_grant_ticket[vfo], consent.ticket, sizeof(ptt_grant_ticket[vfo]));
   int session =
      db_ptt_start_supervised(masterdb, talker->chatname, scope, (double)vfos[vfo].freq, vfo_mode_name(rr_get_mode(vfo)), (int)rr_get_width(vfo), power,
      ptt_recording_file[vfo][0] ? ptt_recording_file[vfo] : NULL, recording_id ? recording_id : "", rrserver_relay_client_server(talker), consent.ticket,
      consent.elmers);

   if (session < 0) {
      Log(LOG_WARN, "ptt", "PTT log: failed to start session for %s ticket=%s", talker->chatname, consent.ticket);

      return;
   }

   Log(LOG_AUDIT, "ptt", "TX started user=%s session=%d rig=%s vfo=%s room=%s freq=%.0fHz mode=%s width=%dHz power=%.3fW ticket=%s elmers=%s",
      talker->chatname, session, scope, vfo_name(vfo), talker->ptt_room,
      (double)vfos[vfo].freq, vfo_mode_name(rr_get_mode(vfo)), (int)rr_get_width(vfo), (double)power,
      consent.ticket, consent.elmers);
   ptt_log_session[vfo] = session;
   ptt_log_owner[vfo] = talker;
   talker->ptt_session = session;
   Log(LOG_DEBUG, "ptt", "PTT log: session %d opened for %s on VFO %s @ %.0f Hz ticket=%s", session, talker->chatname, vfo_name(vfo), (double)vfos[vfo].freq,
      consent.ticket);
#else
   (void)talker;
   (void)vfo;
   (void)recording_id;
#endif
}

// Close the ptt_log row for the VFO, log how long they transmitted
// NB: `talker' may be NULL (e.g. is_ptt already cleared before the key-up
// reaches us, or TOT/fault/disconnect forced TX off). The per-VFO session
// table is the source of truth; the talker is only a fallback lookup.
static void ptt_log_stop(rrconn_t *talker, rr_vfo_t vfo, const char *reason) {
#ifdef  USE_SQLITE

   if (!masterdb || vfo < 0 || vfo >= MAX_VFOS) {
      return;
   }

   if (!talker) {
      talker = ptt_log_owner[vfo];
   }
   ptt_log_owner[vfo] = NULL;
   int session = ptt_log_session[vfo];

   if (session <= 0 && talker) {
      session = talker->ptt_session;   // fallback
   }

   if (session <= 0) {
      return;
   }
   ptt_log_session[vfo] = -1;

   if (talker) {
      talker->ptt_session = 0;
   }

   int secs = -1;

   if (!db_ptt_stop(masterdb, session, &secs, reason)) {
      Log(LOG_WARN, "ptt", "PTT log: failed to close session %d for %s ticket=%s", session, (talker ? talker->chatname : "unknown"), ptt_grant_ticket[vfo]);

      return;
   }

   Log(LOG_AUDIT, "ptt", "TX session closed user=%s session=%d rig=%s vfo=%s room=%s duration=%ds reason=%s ticket=%s",
      talker ? talker->chatname : "unknown", session,
      rig.ptt_rig ? rr_rig_registry_public_alias(rig.rigs, rig.ptt_rig) : "unknown",
      vfo_name(vfo), talker ? talker->ptt_room : "unknown",
      secs, reason ? reason : "released", ptt_grant_ticket[vfo]);

   if (secs >= 0) {
      if (talker) {
         talker->session_tx_seconds += (uint64_t) secs;
      }
      // If the talker is unknown at key-up, recover their name from the
      // session row so the log line and quota debit still work.
      const char *who = (talker ? talker->chatname : NULL);
      char *looked_up = NULL;

      if (!who) {
         looked_up = ptt_log_username(session);
         who = looked_up;
      }
      Log(LOG_DEBUG, "ptt", "PTT log: %s was on the air for %d seconds (session %d)%s%s", (who ? who : "unknown"), secs, session, ptt_grant_ticket[vfo][0] ?
         " ticket=" : "", ptt_grant_ticket[vfo]);

      // Quota accounting: debit the session duration from the user's credits
      // when quota.enforce is true. PARITY: sql/sqlite.master.sql tx_credits
      if (cfg_get_bool("quota.enforce", true) && who && secs > 0) {
         if (!db_quota_spend(masterdb, who, secs)) {
            Log(LOG_WARN, "ptt", "TX quota: failed to debit %d credits for %s%s%s", secs, who, ptt_grant_ticket[vfo][0] ? " ticket=" : "", ptt_grant_ticket[vfo]
               );
         } else {
            int left = db_quota_get(masterdb, who);

            Log(LOG_INFO, "ptt", "TX quota: %s spent %d credits, %d remaining%s%s", who, secs, left, ptt_grant_ticket[vfo][0] ? " ticket=" : "",
               ptt_grant_ticket[vfo]);

            if (talker) {
               quota_maybe_warn(talker, left);
            }
         }
      }
      free(looked_up);
   }
#else
   (void)talker;
   (void)vfo;
#endif
}

static bool accounting_locked, accounting_stop_pending;
bool rr_ptt_accounting_locked(void) {
   return accounting_locked;
}

static void accounting_failed(const char *event, const char *data, rrconn_t *peer, void *user) {
   if (accounting_locked || !cfg_get_bool("accounting.strict", false)) {
      return;
   }
   accounting_locked = accounting_stop_pending = true;
   Log(LOG_CRIT, "accounting", "Accounting write failed (%s); TX locked until server restart", data ? data : "unknown");

   for (rrconn_t *client = http_client_list ; client ; client = client->next) {
      if (client->authenticated) {
         ws_send_error(client, "Accounting database write failed: PTT halted and locked until server restart");
      }
   }
}

static void accounting_poll(const char *event, const char *data, rrconn_t *peer, void *user) {
   static uint64_t retry_at;
   uint64_t at = mono_us();

   if (!accounting_stop_pending || (retry_at && at < retry_at)) {
      return;
   }
   /* Run after statement finalization/rollback; stopping PTT also writes usage. */
   accounting_stop_pending = rr_ptt_set_all_off_reason("accounting-failure");
   retry_at = accounting_stop_pending ? at + 1000000 : 0;
}

void rr_ptt_register_accounting_events(void) {
   event_on("accounting.write.failed", accounting_failed, NULL);
   event_on("server.poll", accounting_poll, NULL);
}

bool rr_ptt_check_blocked(void) {
   if (rig.tx_blocked || accounting_locked) {
      return true;
   }

   return false;
}

bool rr_ptt_set_blocked(bool blocked) {
   if (rig.tx_blocked != blocked) {
      Log(LOG_AUDIT, "ptt", "TX interlock %s; rig=%s accounting_locked=%s", blocked ? "blocked" : "unblocked",
         rig.ptt_rig ? rr_rig_registry_public_alias(rig.rigs, rig.ptt_rig) : "unknown", accounting_locked ? "yes" : "no");
   }
   rig.tx_blocked = blocked;

   return blocked;
}

typedef struct pending_ptt {
   struct pending_ptt *next;
   rr_backend_t *backend;
   rr_server_rig_t *radio;
   rrconn_t *owner;
   rr_vfo_t vfo;
   bool state, failed, superseded;
   char *reason;
} pending_ptt_t;
static pending_ptt_t *pending_ptt, *completing_ptt;
static void ptt_worker_closed(const char *event, const char *data, rrconn_t *client, void *user) {
   for (pending_ptt_t *p = pending_ptt ; p ; p = p->next) {
      if (p->owner == client) {
         p->owner = NULL;

         if (p->state) {
            p->superseded = true;
         }
      }
   }

   for (int i = 0 ; i < MAX_VFOS ; i++) {
      if (ptt_log_owner[i] == client) {
         ptt_log_stop(client, i, "disconnected");
         ptt_log_owner[i] = NULL;
      }
   }
}
static void ptt_worker_completed(rr_backend_t *backend, rr_server_vfo_t *vfo, bool failed, void *user) {
   pending_ptt_t *pending = user;
   pending_ptt_t **link = &pending_ptt;
   while (*link && *link != pending) {
      link = &(*link)->next;
   }

   if (*link) {
      *link = pending->next;
   }

   if (dying || pending->superseded) {
      goto finished;
   }

   if (pending->state && (!pending->owner || whos_talking() != pending->owner || rig.ptt_rig != pending->radio)) {
      Log(LOG_WARN, "ptt", "Cancelling backend PTT-on after worker completion: owner=%s current_talker=%s rig_changed=%s vfo=%s", pending->owner ? pending->
         owner->chatname : "(none)", whos_talking() ? whos_talking()->chatname : "(none)", rig.ptt_rig != pending->radio ? "yes" : "no", vfo_name(pending->vfo))
      ;

      if (pending->owner) {
         ws_send_error(pending->owner, "PTT on VFO %s cancelled: transmitter ownership or active rig changed while backend request was pending", vfo_name(
            pending->vfo));
      }
      rr_backend_worker_submit(backend, vfo, RR_BACKEND_PTT, (dict_value_t) {
         .i = false
      }
         , NULL, NULL);
      goto finished;
   }
   pending->failed = failed;
   completing_ptt = pending;
   rr_server_rig_t *previous = rig.ptt_rig;
   rig.ptt_rig = pending->radio;
   bool rejected = rr_ptt_request(pending->vfo, pending->state, pending->reason);
   rig.ptt_rig = previous;
   completing_ptt = NULL;

   if (rejected && pending->state) {
      rr_backend_worker_submit(backend, vfo, RR_BACKEND_PTT, (dict_value_t) {
         .i = false
      }
         , NULL, NULL);

      if (pending->owner) {
         pending->owner->is_ptt = false;
         pending->owner->ptt_vfo = 0;
         ws_send_userinfo(pending->owner, NULL);
         ws_send_error(pending->owner, "Radio backend rejected PTT on VFO %s", vfo_name(pending->vfo));
      }
   } else if (rejected && !pending->state) {
      Log(LOG_CRIT, "ptt", "Hardware release failed; blocking key-down until a release succeeds");
      accounting_stop_pending = true;
      rr_ptt_set_blocked(true);
   }
finished:
   free(pending->reason);
   free(pending);
}

// For CAT to call
bool rr_ptt_request(rr_vfo_t vfo, bool ptt, const char *reason) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return true;
   }
   char msgbuf[HTTP_WS_MAX_MSG + 1];
   rrconn_t *ptt_talker = whos_talking();
   const char *recording_id = NULL;

   if (ptt && cfg_get_bool("accounting.strict", false)) {
#ifdef USE_SQLITE

      if (!masterdb) {
         accounting_failed(NULL, "database not open", NULL, NULL);
      }
#else
      accounting_failed(NULL, "SQLite accounting unavailable", NULL, NULL);
#endif
   }

   rr_server_rig_t *target_rig = rig.ptt_rig ? rig.ptt_rig : rr_rig_registry_default(rig.rigs);

   if (ptt && rr_rig_halted(target_rig)) {
      Log(LOG_AUDIT, "ptt", "TX key-down rejected: rig=%s uuid=%s reason=halted",
         target_rig ? rr_rig_registry_public_alias(rig.rigs, target_rig) : "unknown",
         target_rig ? rr_server_rig_id(target_rig) : "unknown");

      return true;
   }

   if (ptt && rr_ptt_check_blocked()) {
      Log(LOG_WARN, "ptt", "TX key-down rejected: rig=%s reason=%s%s",
         target_rig ? rr_rig_registry_public_alias(rig.rigs, target_rig) : "unknown",
         accounting_locked ? "accounting locked" : "TX interlock blocked",
         accounting_locked && rig.tx_blocked ? " (hardware interlock also blocked)" : "");

      return true;
   }

#ifdef USE_SQLITE

   // Quota enforcement: when quota.enforce is true, the user needs remaining
   // TX credits (tx_credits table) to key up. PARITY: rrserver/database.c
   if (ptt && cfg_get_bool("quota.enforce", true) && masterdb) {
      rrconn_t *caller = whos_talking();

      if (caller) {
         int credits = db_quota_get(masterdb, caller->chatname);

         if (credits <= 0) {
            Log(LOG_AUDIT, "ptt", "TX quota: %s denied TX (%d credits)", caller->chatname, credits);

            return true;
         }
      }
   }

#endif // USE_SQLITE

   rr_backend_t *backend = rr_backend_for_ptt();

   if (backend && backend->worker && !completing_ptt) {
      static bool registered;

      if (!registered) {
         event_on(RR_OBJECT_CLOSE_EVENT, ptt_worker_closed, NULL);
         registered = true;
      }
      bool matching_request_seen = false;

      for (pending_ptt_t *p = pending_ptt ; p ; p = p->next) {
         if (ptt && !p->state && p->backend != backend) {
            return true;
         }

         if (p->backend == backend && p->vfo == vfo && !matching_request_seen) {
            matching_request_seen = true;

            if (!ptt && !p->state) {
               return false;
            }
         }
      }

      pending_ptt_t *pending = calloc(1, sizeof(*pending));

      if (!pending) {
         return true;
      }
      pending->reason = strdup(reason ? reason : "requested");
      char alias[] = {
         'A' + vfo, 0
      };
      rr_server_vfo_t *target = rr_server_vfo_find_alias(backend->owner, alias);

      if (!pending->reason || !target) {
         free(pending->reason);
         free(pending);

         return true;
      }
      pending->backend = backend;
      pending->radio = backend->owner;
      pending->owner = ptt_talker;
      pending->vfo = vfo;
      pending->state = ptt;

      if (rr_backend_worker_submit(backend, target, RR_BACKEND_PTT, (dict_value_t) {
         .i = ptt
      }
         , ptt_worker_completed, pending)) {
         free(pending->reason);
         free(pending);

         return true;
      }

      if (!ptt) {
         for (pending_ptt_t *p = pending_ptt ; p ; p = p->next) {
            if (p->backend == backend && p->vfo == vfo && p->state) {
               p->superseded = true;
            }
         }
      }
      pending->next = pending_ptt;
      pending_ptt = pending;

      return false;
   }

   if (completing_ptt) {
      ptt_talker = completing_ptt->owner;
   }

   // PTT logging: snapshot VFO state on key-down; log TX seconds on key-up.
   // whos_talking() is updated by librrprotocol (cptr->is_ptt) before the
   // rigctl event reaches us, so the talker is already current here.
   if (vfo >= 0 && vfo < MAX_VFOS) {
      if (ptt && ptt_talker) {
         if (!ptt_recording_id[vfo][0]) {
            au_recording_generate_id(ptt_recording_id[vfo], sizeof(ptt_recording_id[vfo]));
         }
         recording_id = ptt_recording_id[vfo][0] ? ptt_recording_id[vfo] : NULL;
         ptt_recording_file[vfo][0] = '\0';
         ptt_prepare_recording_file(vfo, ptt_talker->chatname, recording_id);
         ptt_log_start(ptt_talker, vfo, recording_id);
      } else if (!ptt) {
         // Close the session for whoever was on this VFO (may be gone by now
         // if TOT/fault/disconnect forced TX off)
         recording_id = ptt_recording_id[vfo][0] ? ptt_recording_id[vfo] : NULL;
      }
   }

   // Go through backend.c (rr_ptt_apply) rather than poking the backend api
   // directly. PARITY: rrserver/backend.c rr_ptt_apply()
   // NB: rr_ptt_apply() returns false on SUCCESS, true on failure.
   if (ptt && accounting_locked) {
      return true;
   }

   if (completing_ptt ? completing_ptt->failed : rr_ptt_apply(vfo, ptt)) {
      rr_server_rig_t *failed_rig = completing_ptt ? completing_ptt->radio : rig.ptt_rig;
      const char *failed_alias = failed_rig ? rr_rig_registry_alias(rig.rigs, failed_rig) : "(unresolved)";
      Log(LOG_WARN, "ptt", "PTT %s failed: rig=%s VFO=%s talker=%s path=%s; see backend log; RF %s", ptt ? "ON" : "OFF", failed_alias, vfo_name(vfo), ptt_talker
         ? ptt_talker->chatname : "(none)", completing_ptt ? "backend-worker" : "synchronous", ptt ? "key-down not confirmed" : "release not confirmed");

      // A failed key-down must not leave an open database session or a
      // recording ID that will be attached to a later transmission.
      if (ptt && vfo >= VFO_A && vfo < MAX_VFOS) {
         ptt_log_stop(ptt_talker, vfo, "backend-error");
         ptt_recording_id[vfo][0] = '\0';
         ptt_recording_file[vfo][0] = '\0';
      }

      return true;
   } else {
      if (ptt && completing_ptt) {
         completing_ptt->backend->active_vfo = vfo;

         if (completing_ptt->radio == rr_rig_registry_default(rig.rigs)) {
            active_vfo = vfo;
         }
      }

      if (ptt && accounting_locked) {
         rr_ptt_set_all_off_reason("accounting-failure");

         return true;
      }

      // set or clear the talk timeout
      // Config: rig.tot - max TX time in seconds (default 300) before the
      // clocktick timer halts PTT. PARITY: rrserver/timer.clocktick.c TOT check
      if (ptt) {
         global_tot_time = now + cfg_get_int("rig.tot", 300);
      } else {
         global_tot_time = 0;
      }

      if (!ptt && vfo >= VFO_A && vfo < MAX_VFOS) {
         ptt_log_stop(ptt_talker, vfo, reason);
      }

      if (!ptt || rrserver_media_activate_ptt(vfo, ptt_talker)) {
         rrserver_media_record_ptt(vfo, ptt, ptt_talker, recording_id);
      } else {
         Log(LOG_WARN, "ptt", "TX audio decoder could not be activated on VFO %s", vfo_name(vfo));
      }
   }

   if (!ptt && vfo >= VFO_A && vfo < MAX_VFOS) {
      ptt_recording_id[vfo][0] = '\0';
   }

   // Broadcast immediate cat.state so clients see TX state without waiting
   // for the next backend poll.
   dict *d = dict_new();

   if (d) {
      dict_add(d, "msg.type", "cat");

      if (rig.ptt_rig) {
         dict_add(d, "cat.room", rr_rig_registry_room(rig.rigs, rig.ptt_rig));
      }
      dict_add(d, "cat.state.vfo", vfo_name(vfo));
      dict_add_bool(d, "cat.state.ptt", ptt);
      dict_add_ulong(d, "msg.ts", now);
      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   } else {
      Log(LOG_WARN, "ptt", "Unable to allocate PTT state broadcast (vfo=%s state=%s)", vfo_name(vfo), ptt ? "on" : "off");
   }

   return false;
}

bool rr_ptt_set_reason(rr_vfo_t vfo, bool ptt, const char *reason) {
   return rr_ptt_request(vfo, ptt, reason) ? false : ptt;
}

bool rr_ptt_set(rr_vfo_t vfo, bool ptt) {
   return rr_ptt_set_reason(vfo, ptt, ptt ? "key-down" : "released");
}

bool rr_ptt_toggle(rr_vfo_t vfo) {
   return rr_ptt_set(vfo, !rig.ptt);
}

bool rr_ptt_set_all_off_reason(const char *reason) {
   Log(LOG_AUDIT, "ptt", "PTT forced release requested for all VFOs: reason=%s, talker=%s", reason ? reason : "forced", whos_talking() ? whos_talking()->
      chatname : "(none)");

   bool failed = false;
   rr_server_rig_t *radio = rig.ptt_rig ? rig.ptt_rig : rr_rig_registry_default(rig.rigs);

   for (int i = VFO_A ; i < MAX_VFOS ; i++) {
      if (radio) {
         char alias[2] = {
            (char)('A' + i), 0
         };
         rr_server_vfo_t *object = rr_server_vfo_find_alias(radio, alias);

         if (!object || !rr_backend_vfo_supported(radio, object)) {
            continue;
         }
      }

      if (rr_ptt_request((rr_vfo_t) i, false, reason ? reason : "forced")) {
         failed = true;
      }
   }

   if (!failed) {
      global_tot_time = 0;
   }

   // Clear the talker's TX state AFTER the VFO loop so ptt_log_stop() above
   // still sees the talker for the quota debit / log line. is_ptt is
   // otherwise only updated by srv.rigctl.c on a client cat.ptt message, so
   // a forced TX-off (TOT, fault, thermal, mute, admin noob-halt) would
   // leave a phantom talker holding the channel until they bounce PTT.
   rrconn_t *talker = whos_talking();

   if (talker && !failed) {
      talker->is_ptt = false;
      talker->ptt_vfo = 0;
      ws_send_userinfo(talker, NULL);
   }

   return failed;
}

bool rr_ptt_set_all_off(void) {
   return rr_ptt_set_all_off_reason("forced");
}

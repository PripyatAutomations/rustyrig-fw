//
// rrserver/database.accounting.c: Audit, PTT, chat and quota accounting writes.
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/database.h>
#include <rrserver/usage.h>

#ifdef USE_SQLITE
/* Emit a server-owned safety event; listeners latch immediately and defer any database-writing PTT cleanup until after this statement is finalized. */
static int accounting_prepare(sqlite3 *db, const char *sql, int bytes, sqlite3_stmt **stmt, const char **tail, const char *operation) {
   int result = sqlite3_prepare_v2(db, sql, bytes, stmt, tail);

   if (result != SQLITE_OK) {
      event_emit("accounting.write.failed", NULL, operation);
   }

   return result;
}

/* Step an accounting statement and signal safety listeners on write failure. */
static int accounting_step(sqlite3_stmt *stmt, const char *operation) {
   int result = sqlite3_step(stmt);

   if (result != SQLITE_DONE && result != SQLITE_ROW) {
      event_emit("accounting.write.failed", NULL, operation);
   }

   return result;
}

/* Execute accounting SQL and signal safety listeners on write failure. */
static int accounting_exec(sqlite3 *db, const char *sql, int (*callback) (void *, int, char **, char **), void *user, char **error, const char *operation) {
   int result = sqlite3_exec(db, sql, callback, user, error);

   if (result != SQLITE_OK) {
      event_emit("accounting.write.failed", NULL, operation);
   }

   return result;
}

/* Audit details may contain strings supplied by peers or external devices.
 * Bound and flatten them centrally before they enter the database. */
static void audit_field_copy(char *dst, size_t size, const char *src) {
   if (!size) return;
   size_t n = 0;
   for (const unsigned char *p = (const unsigned char *)(src ? src : "");
      *p && n + 1 < size; ++p) {
      dst[n++] = (*p < 32 || *p == 127) ? ' ' : (char)*p;
   }
   dst[n] = '\0';
}

/* Persist an attributed audit record using bound values. */
bool db_add_audit_event(sqlite3 *db, const char *username, const char *event_type, const char *details) {
   if (!db || !username || !event_type || !details) {
      return false;
   }
   char safe_user[128], safe_type[96], safe_details[2048];
   audit_field_copy(safe_user, sizeof(safe_user), username);
   audit_field_copy(safe_type, sizeof(safe_type), event_type);
   audit_field_copy(safe_details, sizeof(safe_details), details);
   const char *sql = "INSERT INTO audit_log (username, event_type, details) VALUES (?, ?, ?);";

   sqlite3_stmt *stmt;

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_add_audit_event") != SQLITE_OK) {
      Log(LOG_CRIT, "db", "failed preparing statement in db_add_audit_event: %s", sqlite3_errmsg(db));

      return false;
   }
   sqlite3_bind_text(stmt, 1, safe_user, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 2, safe_type, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 3, safe_details, -1, SQLITE_TRANSIENT);

   int result = accounting_step(stmt, "db_add_audit_event");
   bool success = result == SQLITE_DONE;

   if (!success) {
      Log(LOG_WARN, "db", "Failed saving audit event %s: SQLite %d: %s", event_type, result, sqlite3_errmsg(db));
   }
   sqlite3_finalize(stmt);

   return success;
}

/* Create a PTT log row with user origin, RF scope and recording identity. */
int db_ptt_start(sqlite3 *db, const char *username, const char *vfo, double frequency, const char *mode, int bandwidth, float power, const char *record_file,
   const char *recording_id, const char *server) {
   return db_ptt_start_supervised(db, username, vfo, frequency, mode, bandwidth, power, record_file, recording_id, server, "", "");
}

int db_ptt_start_supervised(sqlite3 *db, const char *username, const char *vfo, double frequency, const char *mode, int bandwidth, float power, const char *
   record_file, const char *recording_id, const char *server, const char *ticket, const char *elmers) {
   if (!db || !username || !mode || !recording_id) {
      return -1;
   }
   // XXX: Add a random session key so we don't have to trust user supplied rowids! ;)
   const char *sql =
      "INSERT INTO ptt_log (username, vfo, frequency, mode, bandwidth, power, record_file, recording_id, server, supervision_ticket, supervising_elmers) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_ptt_start") != SQLITE_OK) {
      Log(LOG_CRIT, "db", "failed preparing statement in db_ptt_start: %s", sqlite3_errmsg(db));

      return -1;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 2, vfo ? vfo : "", -1, SQLITE_STATIC);
   sqlite3_bind_double(stmt, 3, frequency);
   sqlite3_bind_text(stmt, 4, mode, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 5, bandwidth);
   sqlite3_bind_double(stmt, 6, power);

   if (record_file && *record_file) {
      sqlite3_bind_text(stmt, 7, record_file, -1, SQLITE_STATIC);
   } else {
      sqlite3_bind_null(stmt, 7);
   }
   sqlite3_bind_text(stmt, 8, recording_id, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 9, server, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 10, ticket ? ticket : "", -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 11, elmers ? elmers : "", -1, SQLITE_STATIC);

   if (accounting_step(stmt, "db_ptt_start") != SQLITE_DONE) {
      sqlite3_finalize(stmt);

      return -1;
   }
   int row_id = (int)sqlite3_last_insert_rowid(db);
   sqlite3_finalize(stmt);

   // Pass the session key
   return row_id;               // Caller should store this to end the session
}

// Close out a PTT session row: stamp end_time and duration (seconds).
// Returns the duration via *duration_secs when non-NULL (so callers can log
// how long the user transmitted); -1 if the row wasn't found.
bool db_ptt_stop(sqlite3 *db, int session_id, int *duration_secs, const char *stop_reason) {
   if (!db || session_id < 0) {
      return false;
   }

   if (accounting_exec(db, "SAVEPOINT ptt_usage", NULL, NULL, NULL, "db_ptt_stop") != SQLITE_OK) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql =
      "UPDATE ptt_log SET end_time=CURRENT_TIMESTAMP, duration=MAX(0,CAST(ROUND((julianday('now')-julianday(start_time))*86400) AS INTEGER)),stop_reason=? WHERE id=? AND end_time IS NULL";
   bool ok = accounting_prepare(db, sql, -1, &stmt, NULL, "db_ptt_stop") == SQLITE_OK;

   if (ok) {
      sqlite3_bind_text(stmt, 1, stop_reason && *stop_reason ? stop_reason : "released", -1, SQLITE_STATIC);
      sqlite3_bind_int(stmt, 2, session_id);
      ok = accounting_step(stmt, "db_ptt_stop") == SQLITE_DONE && sqlite3_changes(db) == 1;
   }
   sqlite3_finalize(stmt);
   stmt = NULL;
   int duration = -1;

   if (ok) {
      const char *read =
         "SELECT username,duration,MAX(0,MIN(duration,unixepoch(end_time)-MAX(unixepoch(start_time),coalesce((SELECT MAX(reset_at,tx_reset_at) FROM user_usage WHERE name=ptt_log.username),0)))) FROM ptt_log WHERE id=?";
      ok = accounting_prepare(db, read, -1, &stmt, NULL, "db_ptt_stop") == SQLITE_OK;

      if (ok) {
         sqlite3_bind_int(stmt, 1, session_id);
         ok = accounting_step(stmt, "db_ptt_stop") == SQLITE_ROW;

         if (ok) {
            duration = sqlite3_column_int(stmt, 1);
            ok = db_usage_add_tx(db, (const char *)sqlite3_column_text(stmt, 0), sqlite3_column_int64(stmt, 2));
         }
      }
      sqlite3_finalize(stmt);
   }

   if (!ok) {
      accounting_exec(db, "ROLLBACK TO ptt_usage", NULL, NULL, NULL, "db_ptt_stop");
      accounting_exec(db, "RELEASE ptt_usage", NULL, NULL, NULL, "db_ptt_stop");
      Log(LOG_WARN, "db", "PTT session/usage close failed for %d: %s", session_id, sqlite3_errmsg(db));

      return false;
   }

   if (accounting_exec(db, "RELEASE ptt_usage", NULL, NULL, NULL, "db_ptt_stop") != SQLITE_OK) {
      return false;
   }

   if (duration_secs) {
      *duration_secs = duration;
   }

   return true;
}

/* Persist a timestamped chat message for bounded room replay. */
bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src, const char *msg_dest, const char *msg_type, const char *msg_data) {
   if (!db || !msg_src || !msg_type || !msg_data) {
      Log(LOG_CRIT, "db", "invalid arguments db:<%p> ts:%lld src:<%p> dest:<%p> type:<%p> data:<%p>", db, (long long)msg_ts, msg_src, msg_dest, msg_type,
         msg_data);

      return false;
   }

   /* Ephemeral receiver rooms are live audio sessions, not chat archives. */
   if (msg_dest && strlen(msg_dest) == 13 && !g_ascii_strncasecmp(msg_dest, "#sdr-", 5)) {
      bool valid = true;
      for (const char *p = msg_dest + 5; *p; ++p)
         if (!g_ascii_isxdigit(*p)) valid = false;
      if (valid) return true;
   }

   const char *sql = "INSERT INTO chat_log " "(msg_ts, msg_src, msg_dest, msg_type, msg_data) " "VALUES (?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "failed preparing statement in db_add_chat_msg: %s", sqlite3_errmsg(db));

      return false;
   }

   sqlite3_bind_int64(stmt, 1, (sqlite3_int64) msg_ts);
   sqlite3_bind_text(stmt, 2, msg_src, -1, SQLITE_TRANSIENT);

   if (msg_dest) {
      sqlite3_bind_text(stmt, 3, msg_dest, -1, SQLITE_TRANSIENT);
   } else {
      sqlite3_bind_null(stmt, 3);
   }

   sqlite3_bind_text(stmt, 4, msg_type, -1, SQLITE_TRANSIENT);
   sqlite3_bind_text(stmt, 5, msg_data, -1, SQLITE_TRANSIENT);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);

   if (!success) {
      Log(LOG_CRIT, "db", "db_add_chat_msg failed: %s", sqlite3_errmsg(db));
   }

   sqlite3_finalize(stmt);

   return success;
}

// Apply lightweight migrations for schema added after a database was first
// created. Safe to call on every start: each statement is a no-op if the
// column/table already exists.
// Remaining TX credits (TX seconds) for a user, or -1 if they have no row.
int db_quota_get(sqlite3 *db, const char *username) {
   if (!db || !username) {
      return -1;
   }
   const char *sql = "SELECT credits FROM tx_credits WHERE name = ?;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_get: prepare failed: %s", sqlite3_errmsg(db));

      return -1;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

   int credits = -1;

   if (sqlite3_step(stmt) == SQLITE_ROW) {
      credits = sqlite3_column_int(stmt, 0);
   }
   sqlite3_finalize(stmt);

   return credits;
}

// Debit credits for a finished PTT session. Credits are allowed to go
// negative so the books reflect the overage rather than hiding it.
bool db_quota_spend(sqlite3 *db, const char *username, int secs) {
   if (!db || !username || secs <= 0) {
      return false;
   }
   const char *sql =
      "INSERT INTO tx_credits (name, credits) VALUES (?, -?) "
      "ON CONFLICT(name) DO UPDATE SET " "credits = credits + excluded.credits, " "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_quota_spend") != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_spend: prepare failed: %s", sqlite3_errmsg(db));

      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, secs);

   bool success = (accounting_step(stmt, "db_quota_spend") == SQLITE_DONE);
   sqlite3_finalize(stmt);

   if (!success) {
      Log(LOG_CRIT, "db", "db_quota_spend: failed debiting %d credits for %s", secs, username);
   }

   return success;
}

// Grant (or with a negative amount, revoke) credits. Creates the row if
// the user has none yet.
bool db_quota_add(sqlite3 *db, const char *username, int credits) {
   if (!db || !username) {
      return false;
   }
   const char *sql =
      "INSERT INTO tx_credits (name, credits) VALUES (?, ?) "
      "ON CONFLICT(name) DO UPDATE SET " "credits = credits + excluded.credits, " "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_quota_add") != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_add: prepare failed: %s", sqlite3_errmsg(db));

      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, credits);

   bool success = (accounting_step(stmt, "db_quota_add") == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

// Set the user's credits to an absolute value (creates the row if needed).
bool db_quota_set(sqlite3 *db, const char *username, int credits) {
   if (!db || !username) {
      return false;
   }
   const char *sql =
      "INSERT INTO tx_credits (name, credits) VALUES (?, ?) " "ON CONFLICT(name) DO UPDATE SET " "credits = excluded.credits, " "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_quota_set") != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_set: prepare failed: %s", sqlite3_errmsg(db));

      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, credits);

   bool success = (accounting_step(stmt, "db_quota_set") == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

// Iterate all credit rows. Calls cb(name, credits, user) per row; stop when
// cb returns false. Returns false on db errors.
bool db_quota_list(sqlite3 *db, int (*cb) (const char *name, int credits, void *user), void *user) {
   if (!db || !cb) {
      return false;
   }
   // All users with their credits; users without a tx_credits row show 0
   const char *sql = "SELECT u.name, COALESCE(c.credits, 0) " "FROM users u LEFT JOIN tx_credits c ON c.name = u.name " "ORDER BY u.name;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_list: prepare failed: %s", sqlite3_errmsg(db));

      return false;
   }

   int rc;

   while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(stmt, 0);
      int credits = sqlite3_column_int(stmt, 1);

      if (!cb(name, credits, user)) {
         break;
      }
   }
   sqlite3_finalize(stmt);

   return(rc == SQLITE_DONE || rc == SQLITE_ROW);
}

/* Usage counters saturate at SQLite's signed integer limit, never wrap to REAL. */
bool db_usage_init(sqlite3 *db) {
   if (!db) {
      return false;
   }

   return sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS user_usage (name TEXT PRIMARY KEY COLLATE NOCASE, reset_at INTEGER NOT NULL DEFAULT (unixepoch()), tx_text_bytes INTEGER NOT NULL DEFAULT 0 CHECK(tx_text_bytes >= 0), tx_text_frames INTEGER NOT NULL DEFAULT 0 CHECK(tx_text_frames >= 0), tx_binary_bytes INTEGER NOT NULL DEFAULT 0 CHECK(tx_binary_bytes >= 0), tx_binary_frames INTEGER NOT NULL DEFAULT 0 CHECK(tx_binary_frames >= 0), rx_text_bytes INTEGER NOT NULL DEFAULT 0 CHECK(rx_text_bytes >= 0), rx_text_frames INTEGER NOT NULL DEFAULT 0 CHECK(rx_text_frames >= 0), rx_binary_bytes INTEGER NOT NULL DEFAULT 0 CHECK(rx_binary_bytes >= 0), rx_binary_frames INTEGER NOT NULL DEFAULT 0 CHECK(rx_binary_frames >= 0), session_seconds INTEGER NOT NULL DEFAULT 0 CHECK(session_seconds >= 0), tx_seconds INTEGER NOT NULL DEFAULT 0 CHECK(tx_seconds >= 0), bandwidth_remaining INTEGER, tx_reset_at INTEGER NOT NULL DEFAULT 0);",
      NULL, NULL, NULL) == SQLITE_OK;
}

/* Read one account's accumulated traffic and allowance state. */
bool db_usage_get(sqlite3 *db, const char *name, struct rr_usage *usage) {
   if (!db || !name || !*name || !usage) {
      return false;
   }
   memset(usage, 0, sizeof(*usage));
   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db,
      "SELECT tx_text_bytes,tx_text_frames,tx_binary_bytes,tx_binary_frames,rx_text_bytes,rx_text_frames,rx_binary_bytes,rx_binary_frames,session_seconds,tx_seconds,bandwidth_remaining,reset_at FROM user_usage WHERE name=?",
      -1, &stmt, NULL) != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   int result = sqlite3_step(stmt);

   if (result == SQLITE_ROW) {
      usage->traffic.tx_text_bytes = (uint64_t) sqlite3_column_int64(stmt, 0);
      usage->traffic.tx_text_frames = (uint64_t) sqlite3_column_int64(stmt, 1);
      usage->traffic.tx_binary_bytes = (uint64_t) sqlite3_column_int64(stmt, 2);
      usage->traffic.tx_binary_frames = (uint64_t) sqlite3_column_int64(stmt, 3);
      usage->traffic.rx_text_bytes = (uint64_t) sqlite3_column_int64(stmt, 4);
      usage->traffic.rx_text_frames = (uint64_t) sqlite3_column_int64(stmt, 5);
      usage->traffic.rx_binary_bytes = (uint64_t) sqlite3_column_int64(stmt, 6);
      usage->traffic.rx_binary_frames = (uint64_t) sqlite3_column_int64(stmt, 7);
      usage->session_seconds = (uint64_t) sqlite3_column_int64(stmt, 8);
      usage->tx_seconds = (uint64_t) sqlite3_column_int64(stmt, 9);
      usage->bandwidth_limited = sqlite3_column_type(stmt, 10) != SQLITE_NULL;
      usage->bandwidth_remaining = sqlite3_column_int64(stmt, 10);
      usage->reset_at = sqlite3_column_int64(stmt, 11);
   }
   sqlite3_finalize(stmt);

   return result == SQLITE_ROW || result == SQLITE_DONE;
}

/* Accumulate traffic deltas and elapsed seconds for an account. */
bool db_usage_record(sqlite3 *db, const char *name, const struct rr_traffic *delta, uint64_t seconds) {
   if (!db || !name || !*name || !delta || seconds > INT64_MAX) {
      return false;
   }
   const uint64_t values[] = {
      delta->tx_text_bytes, delta->tx_text_frames, delta->tx_binary_bytes, delta->tx_binary_frames, delta->rx_text_bytes, delta->rx_text_frames,
      delta->rx_binary_bytes, delta->rx_binary_frames, seconds
   };

   for (unsigned i = 0 ; i < 9 ; i++) {
      if (values[i] > INT64_MAX) {
         return false;
      }
   }

   uint64_t bytes = 0;
   const uint64_t octets[] = {
      delta->tx_text_bytes, delta->tx_binary_bytes, delta->rx_text_bytes, delta->rx_binary_bytes
   };

   for (unsigned i = 0 ; i < 4 ; i++) {
      if (octets[i] > INT64_MAX - bytes) {
         return false;
      }
      bytes += octets[i];
   }

   sqlite3_stmt *stmt = NULL;
   const char *sql =
      "INSERT INTO user_usage (name,tx_text_bytes,tx_text_frames,tx_binary_bytes,tx_binary_frames,rx_text_bytes,rx_text_frames,rx_binary_bytes,rx_binary_frames,session_seconds) VALUES (?,?,?,?,?,?,?,?,?,?) ON CONFLICT(name) DO UPDATE SET "
      "tx_text_bytes=CASE WHEN tx_text_bytes > 9223372036854775807-excluded.tx_text_bytes THEN 9223372036854775807 ELSE tx_text_bytes+excluded.tx_text_bytes END, "
      "tx_text_frames=CASE WHEN tx_text_frames > 9223372036854775807-excluded.tx_text_frames THEN 9223372036854775807 ELSE tx_text_frames+excluded.tx_text_frames END, "
      "tx_binary_bytes=CASE WHEN tx_binary_bytes > 9223372036854775807-excluded.tx_binary_bytes THEN 9223372036854775807 ELSE tx_binary_bytes+excluded.tx_binary_bytes END, "
      "tx_binary_frames=CASE WHEN tx_binary_frames > 9223372036854775807-excluded.tx_binary_frames THEN 9223372036854775807 ELSE tx_binary_frames+excluded.tx_binary_frames END, "
      "rx_text_bytes=CASE WHEN rx_text_bytes > 9223372036854775807-excluded.rx_text_bytes THEN 9223372036854775807 ELSE rx_text_bytes+excluded.rx_text_bytes END, "
      "rx_text_frames=CASE WHEN rx_text_frames > 9223372036854775807-excluded.rx_text_frames THEN 9223372036854775807 ELSE rx_text_frames+excluded.rx_text_frames END, "
      "rx_binary_bytes=CASE WHEN rx_binary_bytes > 9223372036854775807-excluded.rx_binary_bytes THEN 9223372036854775807 ELSE rx_binary_bytes+excluded.rx_binary_bytes END, "
      "rx_binary_frames=CASE WHEN rx_binary_frames > 9223372036854775807-excluded.rx_binary_frames THEN 9223372036854775807 ELSE rx_binary_frames+excluded.rx_binary_frames END, "
      "session_seconds=CASE WHEN session_seconds > 9223372036854775807-excluded.session_seconds THEN 9223372036854775807 ELSE session_seconds+excluded.session_seconds END, "
      "bandwidth_remaining=CASE WHEN bandwidth_remaining IS NULL THEN NULL WHEN bandwidth_remaining < -9223372036854775807+? THEN -9223372036854775807 ELSE bandwidth_remaining-? END";

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_usage_record") != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

   for (unsigned i = 0 ; i < 9 ; i++) {
      sqlite3_bind_int64(stmt, i + 2, values[i]);
   }

   sqlite3_bind_int64(stmt, 11, bytes);
   sqlite3_bind_int64(stmt, 12, bytes);
   bool ok = accounting_step(stmt, "db_usage_record") == SQLITE_DONE;
   sqlite3_finalize(stmt);

   return ok;
}

/* Accumulate transmitted seconds for an account. */
bool db_usage_add_tx(sqlite3 *db, const char *name, uint64_t seconds) {
   if (!db || !name || !*name || seconds > INT64_MAX) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql =
      "INSERT INTO user_usage(name,tx_seconds) VALUES (?,?) ON CONFLICT(name) DO UPDATE SET tx_seconds=CASE WHEN tx_seconds>9223372036854775807-excluded.tx_seconds THEN 9223372036854775807 ELSE tx_seconds+excluded.tx_seconds END";

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_usage_add_tx") != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   sqlite3_bind_int64(stmt, 2, seconds);
   bool ok = accounting_step(stmt, "db_usage_add_tx") == SQLITE_DONE;
   sqlite3_finalize(stmt);

   return ok;
}

/* Replace or extend an account's remaining bandwidth allowance. */
bool db_usage_bandwidth_set(sqlite3 *db, const char *name, uint64_t bytes, bool add) {
   if (!db || !name || !*name || bytes > INT64_MAX) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql = add ?
      "INSERT INTO user_usage(name,bandwidth_remaining) VALUES (?,?) ON CONFLICT(name) DO UPDATE SET bandwidth_remaining=coalesce(bandwidth_remaining,0)+excluded.bandwidth_remaining WHERE coalesce(bandwidth_remaining,0)<=9223372036854775807-excluded.bandwidth_remaining"
      : "INSERT INTO user_usage(name,bandwidth_remaining) VALUES (?,?) ON CONFLICT(name) DO UPDATE SET bandwidth_remaining=excluded.bandwidth_remaining";

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_usage_bandwidth_set") != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   sqlite3_bind_int64(stmt, 2, bytes);
   bool ok = accounting_step(stmt, "db_usage_bandwidth_set") == SQLITE_DONE && sqlite3_changes(db) == 1;
   sqlite3_finalize(stmt);

   return ok;
}

/* Reset accumulated usage and install the supplied bandwidth allowance. */
bool db_usage_reset(sqlite3 *db, const char *name, uint64_t allowance) {
   if (!db || !name || !*name || allowance > INT64_MAX) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql =
      "INSERT INTO user_usage(name,bandwidth_remaining) VALUES (?,?) ON CONFLICT(name) DO UPDATE SET reset_at=unixepoch(),tx_reset_at=unixepoch(),bandwidth_remaining=excluded.bandwidth_remaining,tx_text_bytes=0,tx_text_frames=0,tx_binary_bytes=0,tx_binary_frames=0,rx_text_bytes=0,rx_text_frames=0,rx_binary_bytes=0,rx_binary_frames=0,session_seconds=0,tx_seconds=0";

   if (accounting_prepare(db, sql, -1, &stmt, NULL, "db_usage_reset") != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   sqlite3_bind_int64(stmt, 2, allowance);
   bool ok = accounting_step(stmt, "db_usage_reset") == SQLITE_DONE;
   sqlite3_finalize(stmt);

   return ok;
}

/* Reset accumulated transmitted seconds without resetting traffic totals. */
bool db_usage_reset_tx(sqlite3 *db, const char *name) {
   if (!db || !name || !*name) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;

   if (accounting_prepare(db,
      "INSERT INTO user_usage(name,tx_reset_at) VALUES (?,unixepoch()) ON CONFLICT(name) DO UPDATE SET tx_seconds=0,tx_reset_at=unixepoch()", -1, &stmt, NULL,
      "db_usage_reset_tx") != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   bool ok = accounting_step(stmt, "db_usage_reset_tx") == SQLITE_DONE;
   sqlite3_finalize(stmt);

   return ok;
}
#endif /* USE_SQLITE */

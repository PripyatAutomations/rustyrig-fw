//
// rrserver/database.c: sqlite3 database stuff
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
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/database.h>

#ifdef  USE_SQLITE
#include <sqlite3.h>

// database handle
sqlite3 *masterdb = NULL;

sqlite3 *db_open(const char *path) {
   if (!path) {
      return NULL;
   }

   if (masterdb) {
      Log(LOG_CRIT, "db", "Master database already open");

      return masterdb;
   }
   sqlite3 *db = NULL;

   if (sqlite3_open(path, &db) == SQLITE_OK) {
      return db;
   }

   return NULL;
}

bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled, const char *password, const char *email,
                 int maxsessions, const char *permissions) {
   if (!db || !name || !password || !email || !permissions) {
      return true;
   }
   const char *sql = "INSERT INTO users "
                     "(uid, name, enabled, password, email, maxsessions, permissions) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_add_user: %s", sqlite3_errmsg(db) );

      return false;
   }
   sqlite3_bind_int(stmt, 1, uid);
   sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
   sqlite3_bind_text(stmt, 4, password, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 5, email ? email : "", -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 6, maxsessions);
   sqlite3_bind_text(stmt, 7, permissions, -1, SQLITE_STATIC);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

/*
 * db_get_users: load the users table into the http_users[] array used by the
 * auth code in librrprotocol. Called via the "authdb.load" event when
 * net.http.authdb-dynamic is true (see srv.auth.passdb.c: http_reload_users())
 * and after db_add_user() changes.
 */
int db_get_users(sqlite3 *db) {
   if (!db) {
      return -1;
   }
   const char *sql = "SELECT uid, name, enabled, password, email, maxsessions, permissions FROM users;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_get_users: prepare failed: %s", sqlite3_errmsg(db));
      return -1;
   }

   // Reset any existing users before (re)loading
   memset( http_users, 0, sizeof(http_users) );
   int user_count = 0;
   int rc;

   while ( (rc = sqlite3_step(stmt) ) == SQLITE_ROW && user_count < HTTP_MAX_USERS) {
      int uid = sqlite3_column_int(stmt, 0);
      const char *name = (const char *)sqlite3_column_text(stmt, 1);
      bool enabled = sqlite3_column_int(stmt, 2) != 0;
      const char *pass = (const char *)sqlite3_column_text(stmt, 3);
      const char *email = (const char *)sqlite3_column_text(stmt, 4);
      int maxsessions = sqlite3_column_int(stmt, 5);
      const char *privs = (const char *)sqlite3_column_text(stmt, 6);

      if (uid < 0 || uid >= HTTP_MAX_USERS || !name || name[0] == '\0') {
         Log(LOG_WARN, "db", "db_get_users: skipping invalid row uid:%d", uid);
         continue;
      }

      http_user_t *up = &http_users[uid];

      up->uid = uid;
      strlcpy( up->name, name, sizeof(up->name) );
      up->enabled = enabled;

      if (pass) {
         strlcpy( up->pass, pass, sizeof(up->pass) );
      }

      if (email) {
         strlcpy( up->email, email, sizeof(up->email) );
      }

      if (maxsessions < 1 || maxsessions > HTTP_MAX_SESSIONS) {
         Log(LOG_WARN, "db", "db_get_users: user %s has invalid maxsessions: %d, defaulting to 1", up->name, maxsessions);
         maxsessions = 1;
      }
      up->max_sessions = maxsessions;

      if (privs) {
         strlcpy( up->privs, privs, sizeof(up->privs) );
      }

      Log(LOG_DEBUG, "db", "db_get_users: uid=%d, user=%s, email=%s, enabled=%s, privs=%s, max_sessions=%d",
         uid, up->name, (up->email[0] != '\0' ? up->email : "none"), (up->enabled ? "true" : "false"),
         (up->privs[0] != '\0' ? up->privs : "none"), up->max_sessions);
      user_count++;
   }

   if (rc != SQLITE_DONE) {
      Log(LOG_CRIT, "db", "db_get_users: iteration failed: %s", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return -1;
   }
   sqlite3_finalize(stmt);

   Log(LOG_INFO, "db", "Loaded %d users from database", user_count);
   return user_count;
}

bool db_add_audit_event(sqlite3 *db, const char *username, const char *event_type, const char *details) {
   if (!db || !username || !event_type || !details) {
      return false;
   }
   const char *sql = "INSERT INTO audit_log (username, event_type, details) VALUES (?, ?, ?);";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_add_audit_event: %s", sqlite3_errmsg(db) );

      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 2, event_type, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 3, details ? details : "", -1, SQLITE_STATIC);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

int db_ptt_start(sqlite3 *db, const char *username, const char *vfo, double frequency, const char *mode, int bandwidth,
                 float power, const char *record_file) {
   if (!db || !username || !mode || !record_file) {
      return -1;
   }
   // XXX: Add a random session key so we don't have to trust user supplied rowids! ;)
   const char *sql =
      "INSERT INTO ptt_log (username, vfo, frequency, mode, bandwidth, power, record_file) "
      "VALUES (?, ?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_ptt_start: %s", sqlite3_errmsg(db) );

      return -1;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 2, vfo ? vfo : "", -1, SQLITE_STATIC);
   sqlite3_bind_double(stmt, 3, frequency);
   sqlite3_bind_text(stmt, 4, mode, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 5, bandwidth);
   sqlite3_bind_double(stmt, 6, power);
   sqlite3_bind_text(stmt, 7, record_file, -1, SQLITE_STATIC);

   if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);

      return -1;
   }
   int row_id = (int)sqlite3_last_insert_rowid(db);
   sqlite3_finalize(stmt);

   // Pass the session key
   return row_id;   // Caller should store this to end the session
}

// Close out a PTT session row: stamp end_time and duration (seconds).
// Returns the duration via *duration_secs when non-NULL (so callers can log
// how long the user transmitted); -1 if the row wasn't found.
bool db_ptt_stop(sqlite3 *db, int session_id, int *duration_secs) {
   if (!db || session_id < 0) {
      return false;
   }
   // SQLite julianday('now') resolution is ~ms; round to whole seconds.
   const char *sql =
      "UPDATE ptt_log "
      "SET end_time = CURRENT_TIMESTAMP, "
      "    duration = CAST(ROUND( (julianday('now') - julianday(start_time)) * 86400 ) AS INTEGER) "
      "WHERE id = ?;";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_ptt_stop: %s", sqlite3_errmsg(db) );

      return false;
   }
   sqlite3_bind_int(stmt, 1, session_id);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0);
   sqlite3_finalize(stmt);

   if (!success) {
      return false;
   }

   if (duration_secs) {
      // Read back the computed duration
      const char *sel = "SELECT duration FROM ptt_log WHERE id = ?;";

      if (sqlite3_prepare_v2(db, sel, -1, &stmt, NULL) != SQLITE_OK) {
         Log( LOG_WARN, "db", "db_ptt_stop: reading back duration failed: %s", sqlite3_errmsg(db) );
         *duration_secs = -1;
         return true;
      }
      sqlite3_bind_int(stmt, 1, session_id);

      if (sqlite3_step(stmt) == SQLITE_ROW) {
         *duration_secs = sqlite3_column_int(stmt, 0);
      } else {
         *duration_secs = -1;
      }
      sqlite3_finalize(stmt);
   }
   return true;
}

bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src,
                     const char *msg_dest, const char *msg_type,
                     const char *msg_data) {
   if (!db || !msg_src || !msg_type || !msg_data) {
      Log(LOG_CRIT, "db", "invalid arguments db:<%p> ts:%lld src:<%p> dest:<%p> type:<%p> data:<%p>",
         db, (long long)msg_ts, msg_src, msg_dest, msg_type, msg_data);
      return false;
   }

   const char *sql =
      "INSERT INTO chat_log "
      "(msg_ts, msg_src, msg_dest, msg_type, msg_data) "
      "VALUES (?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "failed preparing statement in db_add_chat_msg: %s",
         sqlite3_errmsg(db));
      return false;
   }

   sqlite3_bind_int64(stmt, 1, (sqlite3_int64)msg_ts);
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
void db_migrate(sqlite3 *db) {
   if (!db) {
      return;
   }
   char *err = NULL;

   // ptt_log: vfo (VFO letter keyed) & duration (TX seconds, set at key-down)
   if (sqlite3_exec(db, "ALTER TABLE ptt_log ADD COLUMN vfo TEXT;", NULL, NULL, &err) != SQLITE_OK) {
      if (err && strstr(err, "duplicate column") == NULL) {
         Log(LOG_WARN, "db", "db_migrate: adding ptt_log.vfo failed: %s", err);
      }
      sqlite3_free(err);
      err = NULL;
   }
   if (sqlite3_exec(db, "ALTER TABLE ptt_log ADD COLUMN duration INTEGER;", NULL, NULL, &err) != SQLITE_OK) {
      if (err && strstr(err, "duplicate column") == NULL) {
         Log(LOG_WARN, "db", "db_migrate: adding ptt_log.duration failed: %s", err);
      }
      sqlite3_free(err);
      err = NULL;
   }

   // ptt_credits: remaining TX seconds per user (PTT quota accounting)
   if (sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS ptt_credits ("
      "   username TEXT PRIMARY KEY,"
      "   credits INTEGER NOT NULL DEFAULT 0,"
      "   updated DATETIME DEFAULT CURRENT_TIMESTAMP"
      ");", NULL, NULL, &err) != SQLITE_OK) {
      Log(LOG_WARN, "db", "db_migrate: creating ptt_credits failed: %s", err ? err : "?");
      sqlite3_free(err);
      err = NULL;
   }
}

// Remaining PTT credits (TX seconds) for a user, or -1 if they have no row.
int db_quota_get(sqlite3 *db, const char *username) {
   if (!db || !username) {
      return -1;
   }
   const char *sql = "SELECT credits FROM ptt_credits WHERE username = ?;";

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
      "INSERT INTO ptt_credits (username, credits) VALUES (?, -?) "
      "ON CONFLICT(username) DO UPDATE SET "
      "credits = credits + excluded.credits, "
      "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_spend: prepare failed: %s", sqlite3_errmsg(db));
      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, secs);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
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
      "INSERT INTO ptt_credits (username, credits) VALUES (?, ?) "
      "ON CONFLICT(username) DO UPDATE SET "
      "credits = credits + excluded.credits, "
      "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_add: prepare failed: %s", sqlite3_errmsg(db));
      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, credits);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

// Set the user's credits to an absolute value (creates the row if needed).
bool db_quota_set(sqlite3 *db, const char *username, int credits) {
   if (!db || !username) {
      return false;
   }
   const char *sql =
      "INSERT INTO ptt_credits (username, credits) VALUES (?, ?) "
      "ON CONFLICT(username) DO UPDATE SET "
      "credits = excluded.credits, "
      "updated = CURRENT_TIMESTAMP;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_set: prepare failed: %s", sqlite3_errmsg(db));
      return false;
   }
   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 2, credits);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
}

// Iterate all credit rows. Calls cb(name, credits, user) per row; stop when
// cb returns false. Returns false on db errors.
bool db_quota_list(sqlite3 *db, int (*cb)(const char *name, int credits, void *user), void *user) {
   if (!db || !cb) {
      return false;
   }
   const char *sql = "SELECT username, credits FROM ptt_credits ORDER BY username;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_quota_list: prepare failed: %s", sqlite3_errmsg(db));
      return false;
   }

   int rc;

   while ( (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(stmt, 0);
      int credits = sqlite3_column_int(stmt, 1);

      if (!cb(name, credits, user) ) {
         break;
      }
   }
   sqlite3_finalize(stmt);

   return (rc == SQLITE_DONE || rc == SQLITE_ROW);
}
#endif	// USE_SQLITE

// Send a chat-style notice to one client as msg_type (e.g. 'privmsg', 'pub').
// Rendered by clients as replay-* messages (see replay_msg_type()); we send
// it with the replay- prefix already applied so it shows in the chat UI.
bool db_send_notice(rrconn_t *cptr, const char *msg_type, const char *text) {
   if (!cptr || !msg_type || !text) {
      return false;
   }
   const char *replay_type = replay_msg_type(msg_type);

   if (!replay_type) {
      Log(LOG_WARN, "db.notice", "db_send_notice: unknown msg_type: %s", msg_type);
      return false;
   }

   dict *d = dict_new();

   if (!d) {
      Log(LOG_CRIT, "db.notice", "db_send_notice: failed to create dict");
      return false;
   }

   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.msg_type", replay_type);
   dict_add(d, "talk.from", "&server");
   dict_add(d, "talk.target", "&localrig");
   dict_add(d, "talk.data", text);
   dict_add_ulong(d, "msg.ts", now);

   bool sent = ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);

   dict_free(d);
   return sent;
}

const char *replay_msg_type(const char *msg_type) {
   if (!msg_type) {
      return NULL;
   }

   if (!strcmp(msg_type, "pub")) {
      return "replay-pub";
   }

   if (!strcmp(msg_type, "action")) {
      return "replay-action";
   }

   if (!strcmp(msg_type, "privmsg")) {
      return "replay-privmsg";
   }

   return NULL;
}

bool db_send_chat_replay(rrconn_t *cptr, const char *channel) {
   if (!cptr || !channel || !masterdb) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: invalid arguments cptr:<%p> channel:<%p> db:<%p>",
         cptr, channel, masterdb);
      return false;
   }

   const char *sql =
      "SELECT msg_id, msg_ts, msg_src, msg_dest, msg_type, msg_data "
      "FROM chat_log "
      "WHERE msg_dest = ? "
      "ORDER BY msg_ts ASC, msg_id ASC;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(masterdb, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: failed preparing statement: %s",
         sqlite3_errmsg(masterdb));
      return false;
   }

   if (sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: failed binding channel");
      sqlite3_finalize(stmt);
      return false;
   }

   /*
    * Send replay-start lazily: only when we actually have replay lines to
    * send. Clients shouldn't get start/complete markers for an empty replay.
    */
   bool started = false;
   bool success = true;
   int rc;

   while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      sqlite3_int64 msg_id = sqlite3_column_int64(stmt, 0);
      time_t msg_ts = (time_t)sqlite3_column_int64(stmt, 1);

      const char *msg_src =
         (const char *)sqlite3_column_text(stmt, 2);

      const char *msg_dest =
         (const char *)sqlite3_column_text(stmt, 3);

      const char *msg_type =
         (const char *)sqlite3_column_text(stmt, 4);

      const char *msg_text =
         (const char *)sqlite3_column_text(stmt, 5);

      const char *replay_type = replay_msg_type(msg_type);

      if (!replay_type) {
         Log(LOG_CRAZY, "db.replay",
            "db_send_chat_replay: skipping msg_id:%lld unknown type:%s",
            (long long)msg_id,
            msg_type ? msg_type : "(null)");
         continue;
      }

      if (!started) {
         dict *start = dict_new();

         if (!start) {
            Log(LOG_CRIT, "db.replay",
               "db_send_chat_replay: failed creating replay-start dict");
            success = false;
            break;
         }

         dict_add(start, "msg.type", "talk");
         dict_add(start, "talk.cmd", "replay-start");
         dict_add(start, "talk.target", channel);

         if (!ws_send_dict(NULL, cptr, start, WEBSOCKET_OP_TEXT)) {
            Log(LOG_CRIT, "db.replay",
               "db_send_chat_replay: failed sending replay-start to cptr:<%p>",
               cptr);
            dict_free(start);
            success = false;
            break;
         }

         dict_free(start);
         started = true;
      }

      dict *msg = dict_new();

      if (!msg) {
         Log(LOG_CRIT, "db.replay",
            "db_send_chat_replay: failed creating dict for msg_id:%lld",
            (long long)msg_id);
         success = false;
         break;
      }

      dict_add(msg, "msg.type", "talk");
      dict_add(msg, "talk.cmd", "msg");
      dict_add(msg, "talk.msg_type", replay_type);

      if (msg_src) {
         dict_add(msg, "talk.from", msg_src);
      }

      if (msg_dest) {
         dict_add(msg, "talk.target", msg_dest);
      }

      if (msg_text) {
         dict_add(msg, "talk.data", msg_text);
      }

      dict_add_ulong(msg, "msg.ts", (unsigned long)msg_ts);

      Log(LOG_CRAZY, "db.replay",
         "replaying msg_id:%lld ts:%lu src:<%s> dest:<%s> type:<%s> data:<%s>",
         (long long)msg_id,
         (unsigned long)msg_ts,
         msg_src ? msg_src : "(null)",
         msg_dest ? msg_dest : "(null)",
         replay_type,
         msg_text ? msg_text : "(null)");

      Log(LOG_CRAZY, "db.replay",
         "sending replay msg_id:%lld to cptr:<%p>",
         (long long)msg_id, cptr);

      bool sent = ws_send_dict(NULL, cptr, msg, WEBSOCKET_OP_TEXT);

      Log(LOG_CRAZY, "db.replay",
         "ws_send_dict replay msg_id:%lld returned <%s>",
         (long long)msg_id,
         sent ? "true" : "false");

      if (!sent) {
         Log(LOG_CRIT, "db.replay",
            "db_send_chat_replay: failed sending msg_id:%lld",
            (long long)msg_id);
         success = false;
      }

      dict_free(msg);

      if (!success) {
         break;
      }
   }

   /*
    * If we broke out of the loop because ws_send_dict() failed,
    * rc will still be SQLITE_ROW. Don't mistake that for a SQLite
    * iteration error.
    */
   if (rc != SQLITE_DONE && success) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: sqlite iteration failed: %s",
         sqlite3_errmsg(masterdb));
      success = false;
   }

   sqlite3_finalize(stmt);

   /*
    * Tell the client that replay is complete.
    *
    * Only send this if we actually sent replay lines (and the replay
    * completed successfully). If the connection failed while sending a
    * message, there's little point in trying to send another message to it.
    */
   if (success && started) {
      dict *complete = dict_new();

      if (!complete) {
         Log(LOG_CRIT, "db.replay",
            "db_send_chat_replay: failed creating replay-complete dict");
         return false;
      }

      dict_add(complete, "msg.type", "talk");
      dict_add(complete, "talk.cmd", "replay-complete");
      dict_add(complete, "talk.target", channel);

      if (!ws_send_dict(NULL, cptr, complete, WEBSOCKET_OP_TEXT)) {
         Log(LOG_CRIT, "db.replay",
            "db_send_chat_replay: failed sending replay-complete to cptr:<%p>",
            cptr);
         success = false;
      }

      dict_free(complete);
   }

   return success;
}

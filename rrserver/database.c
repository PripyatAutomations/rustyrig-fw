//
// database.c: sqlite3 database stuff
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "build_config.h"
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#if	defined(FEATURE_SQLITE)
#include <sqlite3.h>
#include <rrserver/database.h>

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

bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled,
                     const char *password, const char *email,
                     int maxclones, const char *permissions) {
   if (!db || !name || !password || !email || !permissions) {
      return true;
   }

   const char *sql = "INSERT INTO users "
                     "(uid, name, enabled, password, email, maxclones, permissions) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "db", "failed preparing statement in db_add_user: %s", sqlite3_errmsg(db));
      return false;
   }

   sqlite3_bind_int(stmt, 1, uid);
   sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
   sqlite3_bind_text(stmt, 4, password, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 5, email ? email : "", -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 6, maxclones);
   sqlite3_bind_text(stmt, 7, permissions, -1, SQLITE_STATIC);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);
   return success;
}

bool db_add_audit_event(sqlite3 *db, const char *username, const char *event_type, const char *details) {
   if (!db || !username || !event_type || !details) {
      return false;
   }

   const char *sql = "INSERT INTO audit_log (username, event_type, details) VALUES (?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "db", "failed preparing statement in db_add_audit_event: %s", sqlite3_errmsg(db));
      return false;
   }

   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 2, event_type, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 3, details ? details : "", -1, SQLITE_STATIC);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);
   return success;
}

// XXX: We need to add support for pointing to the recording file that will be started
int db_ptt_start(sqlite3 *db, const char *username,
                 double frequency, const char *mode,
                 int bandwidth, float power, const char *record_file) {

   if (!db || !username || !mode || !record_file) {
      return -1;
   }

   const char *sql = "INSERT INTO ptt_log (username, frequency, mode, bandwidth, power, record_file) "
                     "VALUES (?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "db", "failed preparing statement in db_ptt_start: %s", sqlite3_errmsg(db));
      return -1;
   }

   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_double(stmt, 2, frequency);
   sqlite3_bind_text(stmt, 3, mode, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 4, bandwidth);
   sqlite3_bind_double(stmt, 5, power);
   sqlite3_bind_text(stmt, 6, record_file, -1, SQLITE_STATIC);

   if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return -1;
   }

   int row_id = (int)sqlite3_last_insert_rowid(db);
   sqlite3_finalize(stmt);
   return row_id;  // Caller should store this to end the session
}

bool db_ptt_stop(sqlite3 *db, int session_id) {
   if (!db || session_id < 0) {
      return false;
   }

   const char *sql = "UPDATE ptt_log SET end_time = CURRENT_TIMESTAMP WHERE id = ?;";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "db", "failed preparing statement in db_ptt_stop: %s", sqlite3_errmsg(db));
      return false;
   }

   sqlite3_bind_int(stmt, 1, session_id);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);
   return success;
}


bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src, const char *msg_dest, const char *msg_type, const char *msg_data) {
   if (!db || !msg_src || !msg_dest || !msg_type || !msg_data) {
      Log(LOG_WARN, "db", "invalid arguments db:<%x> ts:%lu src:<%x> dest:<%x> type:<%x> data:<%x>",
          db, msg_ts, msg_src, msg_dest, msg_type, msg_data);
      return false;
   }

   const char *sql = "INSERT INTO chat_log (msg_ts, msg_src, msg_dest, msg_type, msg_data) VALUES (?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_WARN, "db", "failed preparing statement in db_add_chat_msg: %s", sqlite3_errmsg(db));
      return false;
   }

   sqlite3_bind_int(stmt, 1, msg_ts);
   sqlite3_bind_text(stmt, 2, msg_src, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 3, msg_dest, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 4, msg_type, -1, SQLITE_STATIC);
   sqlite3_bind_text(stmt, 5, msg_data, -1, SQLITE_STATIC);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);
   return success;
}

bool db_send_chat_replay(http_client_t *cptr, const char *channel) {
   // retrieve the lines from database
   // For first message, send Replay Start message to client
   // send the message to the user
   // cleanup memory allocations
   // send Reply Complete message to client
   return false;
}
#endif

//
// database.c: sqlite3 database stuff
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#if	defined(FEATURE_SQLITE)
#include <sqlite3.h>
#include "inc/database.h"

sqlite3 *masterdb = NULL;

sqlite3 *db_open(const char *path) {
   sqlite3 *db;

   if (sqlite3_open(path, &db) == SQLITE_OK) {
      return db;
   }
   return NULL;
}

bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled,
                     const char *password, const char *email,
                     int maxclones, const char *permissions) {
   const char *sql = "INSERT INTO users "
                     "(uid, name, enabled, password, email, maxclones, permissions) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
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
   const char *sql = "INSERT INTO audit_log (username, event_type, details) VALUES (?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
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
                 int bandwidth, float power) {
   const char *sql = "INSERT INTO ptt_log (username, frequency, mode, bandwidth, power) "
                     "VALUES (?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      return -1;
   }

   sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
   sqlite3_bind_double(stmt, 2, frequency);
   sqlite3_bind_text(stmt, 3, mode, -1, SQLITE_STATIC);
   sqlite3_bind_int(stmt, 4, bandwidth);
   sqlite3_bind_double(stmt, 5, power);

   if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return -1;
   }

   int row_id = (int)sqlite3_last_insert_rowid(db);
   sqlite3_finalize(stmt);
   return row_id;  // Caller should store this to end the session
}

bool db_ptt_stop(sqlite3 *db, int session_id) {
   const char *sql = "UPDATE ptt_log SET end_time = CURRENT_TIMESTAMP WHERE id = ?;";

   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      return false;
   }

   sqlite3_bind_int(stmt, 1, session_id);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);
   return success;
}

#endif

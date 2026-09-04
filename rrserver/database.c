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
                 int maxclones, const char *permissions) {
   if (!db || !name || !password || !email || !permissions) {
      return true;
   }
   const char *sql = "INSERT INTO users "
                     "(uid, name, enabled, password, email, maxclones, permissions) "
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

int db_ptt_start(sqlite3 *db, const char *username, double frequency, const char *mode, int bandwidth, float power,
                 const char *record_file) {
   if (!db || !username || !mode || !record_file) {
      return -1;
   }
   // XXX: Add a random session key so we don't have to trust user supplied rowids! ;)
   const char *sql =
      "INSERT INTO ptt_log (username, frequency, mode, bandwidth, power, record_file) "
      "VALUES (?, ?, ?, ?, ?, ?);";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_ptt_start: %s", sqlite3_errmsg(db) );

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

   // Pass the session key 
   return row_id;   // Caller should store this to end the session
}

bool db_ptt_stop(sqlite3 *db, int session_id) {
   if (!db || session_id < 0) {
      return false;
   }
   const char *sql = "UPDATE ptt_log SET end_time = CURRENT_TIMESTAMP WHERE id = ?;";

   sqlite3_stmt *stmt;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log( LOG_CRIT, "db", "failed preparing statement in db_ptt_stop: %s", sqlite3_errmsg(db) );

      return false;
   }
   sqlite3_bind_int(stmt, 1, session_id);

   bool success = (sqlite3_step(stmt) == SQLITE_DONE);
   sqlite3_finalize(stmt);

   return success;
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
#endif	// USE_SQLITE

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
    * Tell the client that replay is starting.
    */
   dict *start = dict_new();

   if (!start) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: failed creating replay-start dict");
      sqlite3_finalize(stmt);
      return false;
   }

   dict_add(start, "msg.type", "talk");
   dict_add(start, "talk.cmd", "replay-start");
   dict_add(start, "talk.target", channel);

   if (!ws_send_dict(NULL, cptr, start, WEBSOCKET_OP_TEXT)) {
      Log(LOG_CRIT, "db.replay",
         "db_send_chat_replay: failed sending replay-start to cptr:<%p>",
         cptr);
      dict_free(start);
      sqlite3_finalize(stmt);
      return false;
   }

   dict_free(start);

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
    * Only send this if the replay itself completed successfully.
    * If the connection failed while sending a message, there's
    * little point in trying to send another message to it.
    */
   if (success) {
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

//
// rrserver/database.users.c: Account persistence and authentication database loading.
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
/* data_version detects commits from other SQLite connections, including tools.
 * Server account editors already reload after writes; unrelated local audit/chat
 * writes must not rebuild accounts. Never acknowledge a version until its
 * account table was loaded successfully. */
bool db_refresh_users(sqlite3 *db) {
   static sqlite3 *loaded;
   static int version;
   static bool refreshing;
   sqlite3_stmt *stmt = NULL;
   if (!db) return false;
   if (refreshing) return true; /* Session reconciliation can emit protocol events. */
   int rc = sqlite3_prepare_v2(db, "PRAGMA data_version", -1, &stmt, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
   int current = rc == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
   int finished = sqlite3_finalize(stmt);
   if (rc != SQLITE_ROW || finished != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Cannot check live account database: %s", sqlite3_errmsg(db));
      return false;
   }
   if (loaded == db && version == current) return true;
   refreshing = true;
   bool ok = db_get_users(db) >= 0;
   refreshing = false;
   if (ok) {
      loaded = db;
      version = current;
   }
   return ok;
}

/* Check bindings, execution and finalization without logging bound credentials. */
static bool user_statement_finish(sqlite3 *db, sqlite3_stmt *stmt, int rc, const char *operation, const char *name, bool require_row) {
   if (rc == SQLITE_OK) rc = sqlite3_step(stmt);
   bool ok = rc == SQLITE_DONE && (!require_row || sqlite3_changes(db) == 1);
   if (!ok) {
      Log(LOG_CRIT, "db", "%s for %s failed (SQLite %d, changed %d): %s", operation, name, rc, sqlite3_changes(db), sqlite3_errmsg(db));
   }
   int finalized = sqlite3_finalize(stmt);
   if (finalized != SQLITE_OK) {
      Log(LOG_CRIT, "db", "%s for %s finalize failed (SQLite %d): %s", operation, name, finalized, sqlite3_errmsg(db));
      ok = false;
   }
   return ok;
}

/* Insert an account with a caller-supplied password hash. */
bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled, const char *password, const char *email, int maxsessions, const char *permissions) {
   if (!db || !name || !password || !email || !permissions) {
      return false;
   }
   const char *sql = "INSERT INTO users "
      "(uid, name, enabled, password, password_set, email, maxsessions, permissions) " "VALUES (?, ?, ?, ?, unixepoch(), ?, ?, ?);";

   sqlite3_stmt *stmt = NULL;

   int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 1, uid);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, password, -1, SQLITE_STATIC);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 5, email ? email : "", -1, SQLITE_STATIC);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, maxsessions);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 7, permissions, -1, SQLITE_STATIC);

   return user_statement_finish(db, stmt, rc, "db_add_user", name, true);
}

/* Create an account with initial password-change and expiry policy. */
bool db_user_create(sqlite3 *db, int uid, const char *name, bool enabled, const char *password, const char *email, int maxsessions, const char *permissions,
   bool password_change_required, time_t password_expires) {
   if (!db || uid < 0 || !name || !*name || !password || !permissions || maxsessions < 1) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql = "INSERT INTO users "
      "(uid,name,enabled,password,password_set,password_expires,password_change_required,email,maxsessions,permissions) "
      "VALUES (?,?,?,?,unixepoch(),?,?,?, ?,?);";

   int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 1, uid);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, password, -1, SQLITE_TRANSIENT);

   if (password_expires > 0) {
      if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 5, (sqlite3_int64) password_expires);
   } else {
      if (rc == SQLITE_OK) rc = sqlite3_bind_null(stmt, 5);
   }
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 6, password_change_required ? 1 : 0);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 7, email ? email : "", -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 8, maxsessions);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 9, permissions, -1, SQLITE_TRANSIENT);
   return user_statement_finish(db, stmt, rc, "db_user_create", name, true);
}

/* Keep SQL and binding failures visible without logging account credentials. */
bool db_user_edit(sqlite3 *db, const char *name, dict *fields) {
   const char *columns[] = {"email", "permissions", "enabled", "maxsessions", "password_change_required", "password_expires"};
   char sql[512] = "UPDATE users SET ";
   int count = 0;

   if (!db || !name || !*name || !fields) {
      Log(LOG_CRIT, "db", "Account edit unavailable: missing database, target or fields");
      return false;
   }

   for (unsigned i = 0; i < sizeof(columns) / sizeof(columns[0]); i++) {
      if (dict_get_type(fields, columns[i]) != VAL_END) {
         snprintf(sql + strlen(sql), sizeof(sql) - strlen(sql), "%s%s=?", count++ ? "," : "", columns[i]);
      }
   }
   if (!count) return true;
   strlcat(sql, " WHERE name=? COLLATE NOCASE", sizeof(sql));
   sqlite3_stmt *stmt = NULL;
   int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
   int parameter = 1;

   for (unsigned i = 0; rc == SQLITE_OK && i < sizeof(columns) / sizeof(columns[0]); i++) {
      if (dict_get_type(fields, columns[i]) == VAL_END) continue;
      const char *value = dict_get(fields, columns[i], "");
      if (!strcmp(columns[i], "permissions") && !strcasecmp(value, "none")) value = "";
      if (!strcmp(columns[i], "password_expires") && (!*value || !strcmp(value, "0"))) {
         rc = sqlite3_bind_null(stmt, parameter++);
      } else {
         rc = sqlite3_bind_text(stmt, parameter++, value, -1, SQLITE_TRANSIENT);
      }
   }
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, parameter, name, -1, SQLITE_TRANSIENT);
   return user_statement_finish(db, stmt, rc, "Account edit", name, true);
}

/* Reuse the checked editor persistence path for individual account commands. */
bool db_user_set_enabled(sqlite3 *db, const char *name, bool enabled) {
   dict *fields = dict_new();
   if (!fields) return false;
   dict_add(fields, "enabled", enabled ? "1" : "0");
   bool ok = db_user_edit(db, name, fields);
   dict_free(fields);
   return ok;
}

bool db_user_set_privileges(sqlite3 *db, const char *name, const char *privileges) {
   if (!privileges || strlen(privileges) > USER_PRIV_LEN) return false;
   dict *fields = dict_new();
   if (!fields) return false;
   dict_add(fields, "permissions", privileges);
   bool ok = db_user_edit(db, name, fields);
   dict_free(fields);
   return ok;
}

/* Delete an account and its legacy TX credits transactionally. */
bool db_user_remove(sqlite3 *db, const char *name) {
   if (!db || !name || !*name) return false;

   if (sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Account removal BEGIN failed: %s", sqlite3_errmsg(db));
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   int rc = sqlite3_prepare_v2(db, "DELETE FROM users WHERE name=? COLLATE NOCASE;", -1, &stmt, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
   bool ok = user_statement_finish(db, stmt, rc, "Account removal", name, true);

   if (ok) {
      stmt = NULL;
      rc = sqlite3_prepare_v2(db, "DELETE FROM tx_credits WHERE name=? COLLATE NOCASE;", -1, &stmt, NULL);
      if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
      ok = user_statement_finish(db, stmt, rc, "Account credit removal", name, false);
   }

   if (ok && sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Account removal COMMIT failed: %s", sqlite3_errmsg(db));
      ok = false;
   }
   if (!ok && sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Account removal ROLLBACK failed: %s", sqlite3_errmsg(db));
   }
   return ok;
}

/* Replace a password hash and its change/expiry metadata. */
bool db_user_update_password(sqlite3 *db, const char *name, const char *password_hash, bool password_change_required, time_t password_expires) {
   if (!db || !name || !*name || !password_hash || !*password_hash) {
      return false;
   }
   sqlite3_stmt *stmt = NULL;
   const char *sql = "UPDATE users SET password=?, password_set=unixepoch(), password_expires=?, " "password_change_required=? WHERE name=?;";

   int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 1, password_hash, -1, SQLITE_TRANSIENT);

   if (password_expires > 0) {
      if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 2, (sqlite3_int64) password_expires);
   } else {
      if (rc == SQLITE_OK) rc = sqlite3_bind_null(stmt, 2);
   }
   if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 3, password_change_required ? 1 : 0);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, name, -1, SQLITE_TRANSIENT);
   return user_statement_finish(db, stmt, rc, "db_user_update_password", name, true);
}

/* Find the next available persistent account UID. */
int db_user_next_uid(sqlite3 *db) {
   if (!db) {
      return -1;
   }
   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(uid)+1,1) FROM users;", -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Account UID lookup prepare failed: %s", sqlite3_errmsg(db));
      return -1;
   }
   int uid = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
   if (uid < 0) Log(LOG_CRIT, "db", "Account UID lookup step failed: %s", sqlite3_errmsg(db));
   if (sqlite3_finalize(stmt) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "Account UID lookup finalize failed: %s", sqlite3_errmsg(db));
      uid = -1;
   }

   return uid;
}

/*
 * db_get_users: load the users table into the http_users[] array used by the auth code in librrprotocol. Called via the "authdb.load" event when
 * net.http.authdb-dynamic is true (see srv.auth.passdb.c: http_reload_users()) and after db_add_user() changes.
 */
int db_get_users(sqlite3 *db) {
   if (!db) {
      return -1;
   }
   const char *sql = "SELECT uid, name, enabled, password, password_set, password_expires, "
      "password_change_required, email, maxsessions, permissions FROM users;";

   sqlite3_stmt *stmt = NULL;

   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      Log(LOG_CRIT, "db", "db_get_users: prepare failed: %s", sqlite3_errmsg(db));

      return -1;
   }

   /* Reload the persistent fields without dropping live connection state. Authentication keeps pointers into this array, so preserve the fields maintained by
    * the protocol layer while replacing the database-backed account data. */
   http_user_t old_users[HTTP_MAX_USERS];
   memcpy(old_users, http_users, sizeof(old_users));
   memset(http_users, 0, sizeof(http_users));
   int user_count = 0;
   int rc;

   while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && user_count < HTTP_MAX_USERS) {
      int uid = sqlite3_column_int(stmt, 0);
      const char *name = (const char *)sqlite3_column_text(stmt, 1);
      bool enabled = sqlite3_column_int(stmt, 2) != 0;
      const char *pass = (const char *)sqlite3_column_text(stmt, 3);
      time_t password_set = (time_t) sqlite3_column_int64(stmt, 4);
      time_t password_expires = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? 0 : (time_t) sqlite3_column_int64(stmt, 5);
      bool password_change_required = sqlite3_column_int(stmt, 6) != 0;
      const char *email = (const char *)sqlite3_column_text(stmt, 7);
      int maxsessions = sqlite3_column_int(stmt, 8);
      const char *privs = (const char *)sqlite3_column_text(stmt, 9);

      if (uid < 0 || uid >= HTTP_MAX_USERS || !name || name[0] == '\0') {
         Log(LOG_WARN, "db", "db_get_users: skipping invalid row uid:%d", uid);
         continue;
      }

      http_user_t *up = &http_users[uid];

      up->uid = uid;
      up->sessions = old_users[uid].sessions;
      up->is_muted = old_users[uid].is_muted;
      strlcpy(up->name, name, sizeof(up->name));
      up->enabled = enabled;
      up->password_set = password_set;
      up->password_expires = password_expires;
      up->password_change_required = password_change_required;

      if (pass) {
         strlcpy(up->pass, pass, sizeof(up->pass));
      }

      if (email) {
         strlcpy(up->email, email, sizeof(up->email));
      }

      if (maxsessions < 1 || maxsessions > HTTP_MAX_SESSIONS) {
         Log(LOG_WARN, "db", "db_get_users: user %s has invalid maxsessions: %d, defaulting to 1", up->name, maxsessions);
         maxsessions = 1;
      }
      up->max_sessions = maxsessions;

      if (privs) {
         strlcpy(up->privs, privs, sizeof(up->privs));
      }

      Log(LOG_CRAZY, "db", "db_get_users: uid=%d, user=%s, email=%s, enabled=%s, privs=%s, max_sessions=%d", uid, up->name, (up->email[0] != '\0' ? up->email :
         "none"), (up->enabled ? "true" : "false"), (up->privs[0] != '\0' ? up->privs : "none"), up->max_sessions);
      user_count++;
   }

   if (rc != SQLITE_DONE) {
      Log(LOG_CRIT, "db", "db_get_users: iteration failed: %s", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      memcpy(http_users, old_users, sizeof(old_users));

      return -1;
   }
   sqlite3_finalize(stmt);

   http_reconcile_users(old_users);

   Log(LOG_INFO, "db", "Loaded %d users from database", user_count);

   return user_count;
}

#endif /* USE_SQLITE */

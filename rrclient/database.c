/* Persistent client settings and network database. SQL lives in sql/. */
#include <rrclient/database.h>
#include <sqlite3.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

static sqlite3 *client_db;

bool rrclient_database_init(void) {
   if (client_db) return true;
   gchar *dir = g_build_filename(g_get_user_data_dir(), "rustyrig", NULL);
   gchar *path = g_build_filename(dir, "client.db", NULL);
   bool ok = g_mkdir_with_parents(dir, 0700) == 0;
   if (ok) ok = sqlite3_open(path, &client_db) == SQLITE_OK;
   if (!ok) {
      fprintf(stderr, "Client database %s: %s\n", path,
              client_db ? sqlite3_errmsg(client_db) : "cannot create directory");
      goto fail;
   }
   g_chmod(path, 0600);
   sqlite3_busy_timeout(client_db, 250);
   if (sqlite3_exec(client_db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL) != SQLITE_OK) goto fail;
   sqlite3_stmt *stmt = NULL;
   int version = -1;
   if (sqlite3_prepare_v2(client_db, "PRAGMA user_version", -1, &stmt, NULL) == SQLITE_OK &&
       sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int(stmt, 0);
   sqlite3_finalize(stmt);
   if (version == 1) { g_free(path); g_free(dir); return true; }
   if (version != 0) {
      fprintf(stderr, "Unsupported client database schema version %d\n", version);
      goto fail;
   }
   /* Installed schema is preferred; source checkout works without make install. */
   const char *sources[] = { "/var/lib/rustyrig/sql/sqlite.client.sql", "sql/sqlite.client.sql" };
   gchar *sql = NULL;
   for (unsigned i = 0; i < G_N_ELEMENTS(sources) && !sql; i++)
      g_file_get_contents(sources[i], &sql, NULL, NULL);
   if (!sql) {
      fprintf(stderr, "Client SQL schema not found (sql/sqlite.client.sql)\n");
      goto fail;
   }
   char *error = NULL;
   ok = sqlite3_exec(client_db, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK &&
        sqlite3_exec(client_db, sql, NULL, NULL, &error) == SQLITE_OK &&
        sqlite3_exec(client_db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
   if (!ok) {
      sqlite3_exec(client_db, "ROLLBACK", NULL, NULL, NULL);
      fprintf(stderr, "Client SQL initialization failed: %s\n",
              error ? error : sqlite3_errmsg(client_db));
   }
   sqlite3_free(error);
   g_free(sql);
   if (!ok) goto fail;
   g_free(path); g_free(dir);
   return true;
fail:
   if (client_db) sqlite3_close(client_db);
   client_db = NULL;
   g_free(path); g_free(dir);
   return false;
}

void rrclient_database_shutdown(void) {
   if (client_db) sqlite3_close(client_db);
   client_db = NULL;
}

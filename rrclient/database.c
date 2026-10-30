//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Persistent client settings and network database. SQL lives in sql/. */
#include <rrclient/database.h>
#include <sqlite3.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <librustyaxe/core.h>
extern dict *cfg;

static sqlite3 *client_db;

bool rrclient_database_init(void) {
   if (client_db) {
      return true;
   }
   gchar *dir = g_build_filename(g_get_user_state_dir(), "rustyrig", NULL);
   gchar *path = g_build_filename(dir, "client.db", NULL);
   bool ok = g_mkdir_with_parents(dir, 0700) == 0;

   if (ok) {
      ok = sqlite3_open(path, &client_db) == SQLITE_OK;
   }

   if (!ok) {
      fprintf(stderr, "Client database %s: %s\n", path, client_db ? sqlite3_errmsg(client_db) : "cannot create directory");
      goto fail;
   }
   g_chmod(path, 0600);
   sqlite3_busy_timeout(client_db, 250);

   if (sqlite3_exec(client_db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL) != SQLITE_OK) {
      goto fail;
   }
   sqlite3_stmt *stmt = NULL;
   int version = -1;

   if (sqlite3_prepare_v2(client_db, "PRAGMA user_version", -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
      version = sqlite3_column_int(stmt, 0);
   }
   sqlite3_finalize(stmt);

   if (version == 2 || version == 3 || version == 4) {
      if (sqlite3_exec(client_db, "CREATE TABLE IF NOT EXISTS proxies (name TEXT PRIMARY KEY COLLATE NOCASE, url TEXT NOT NULL); CREATE TABLE IF NOT EXISTS themes (name TEXT NOT NULL COLLATE NOCASE, property TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(name,property)); PRAGMA user_version=4", NULL, NULL, NULL) != SQLITE_OK) goto fail;
      g_free(path);
      g_free(dir);

      return true;
   }

   if (version != 0 && version != 1) {
      fprintf(stderr, "Unsupported client database schema version %d\n", version);
      goto fail;
   }
   /* Installed schema is preferred; source checkout works without make install. */
   const char *sources[] = {
      "/var/lib/rustyrig/sql/sqlite.client.sql", "sql/sqlite.client.sql"
   };
   gchar *sql = NULL;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(sources) && !sql ; i++) {
      g_file_get_contents(sources[i], &sql, NULL, NULL);
   }

   if (!sql) {
      fprintf(stderr, "Client SQL schema not found (sql/sqlite.client.sql)\n");
      goto fail;
   }
   char *error = NULL;
   ok = sqlite3_exec(client_db, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK &&
      sqlite3_exec(client_db, sql, NULL, NULL, &error) == SQLITE_OK && sqlite3_exec(client_db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;

   if (!ok) {
      sqlite3_exec(client_db, "ROLLBACK", NULL, NULL, NULL);
      fprintf(stderr, "Client SQL initialization failed: %s\n", error ? error : sqlite3_errmsg(client_db));
   }
   sqlite3_free(error);
   g_free(sql);

   if (!ok) {
      goto fail;
   }
   g_free(path);
   g_free(dir);

   return true;
fail:

   if (client_db) {
      sqlite3_close(client_db);
   }
   client_db = NULL;
   g_free(path);
   g_free(dir);

   return false;
}

void rrclient_database_shutdown(void) {
   if (client_db) {
      sqlite3_close(client_db);
   }
   client_db = NULL;
}

static bool db_exec3(const char *sql, const char *a, const char *b, const char *c) {
   sqlite3_stmt *stmt = NULL;

   if (!client_db || sqlite3_prepare_v2(client_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
      return false;
   }

   if (a) {
      sqlite3_bind_text(stmt, 1, a, -1, SQLITE_TRANSIENT);
   }

   if (b) {
      sqlite3_bind_text(stmt, 2, b, -1, SQLITE_TRANSIENT);
   }

   if (c) {
      sqlite3_bind_text(stmt, 3, c, -1, SQLITE_TRANSIENT);
   }
   bool ok = sqlite3_step(stmt) == SQLITE_DONE;
   sqlite3_finalize(stmt);

   return ok;
}

/* WebSocket URLs with no explicit path use the RustyRig client endpoint. */
char *rrclient_database_normalize_url(const char *url) {
   if (!url) {
      return NULL;
   }

   if (!g_str_has_prefix(url, "ws://") && !g_str_has_prefix(url, "wss://")) {
      return g_strdup(url);
   }
   const char *authority = strstr(url, "://") + 3;
   const char *end = strpbrk(authority, "/?#");

   if (!end) {
      return g_strconcat(url, "/ws/", NULL);
   }

   return g_strdup(url);
}

bool rrclient_database_server_set(const char *name, const char *property, const char *value) {
   if (!name || !*name || !property || !value) {
      return false;
   }
   char *normalized = !strcmp(property, "server.url") ? rrclient_database_normalize_url(value) : NULL;

   if (normalized) {
      value = normalized;
   }

   if (sqlite3_exec(client_db, "SAVEPOINT profile_write", NULL, NULL, NULL) != SQLITE_OK) {
      g_free(normalized);

      return false;
   }
   bool ok = false;

   if (!strcmp(property, "server.url")) {
      ok = db_exec3("INSERT INTO servers(name,url) VALUES(?1,?2) " "ON CONFLICT(name) DO UPDATE SET url=excluded.url", name, value, NULL);
   } else if (!strcmp(property, "server.user") || !strcmp(property, "server.proxy")) {
      ok = db_exec3(!strcmp(property, "server.user") ?
         "UPDATE servers SET username=?2 WHERE name=?1" : "UPDATE servers SET proxy=?2 WHERE name=?1", name, value, NULL);
   } else {
      ok = db_exec3("INSERT INTO server_properties(server_id,property,value) "
         "SELECT id,?2,?3 FROM servers WHERE name=?1 " "ON CONFLICT(server_id,property) DO UPDATE SET value=excluded.value", name, property, value);
   }

   if (ok) {
      ok = sqlite3_exec(client_db, "RELEASE profile_write", NULL, NULL, NULL) == SQLITE_OK;
   } else {
      sqlite3_exec(client_db, "ROLLBACK TO profile_write; RELEASE profile_write", NULL, NULL, NULL);
   }

   if (ok) {
      char *key = g_strdup_printf("server:%s.%s", name, property);
      dict_add(cfg, key, value);
      g_free(key);
   }
   g_free(normalized);

   return ok;
}

bool rrclient_database_network_set(const char *name, const char *members) {
   if (!name || !*name || !members || !client_db) {
      return false;
   }

   if (sqlite3_exec(client_db, "SAVEPOINT network_write", NULL, NULL, NULL) != SQLITE_OK) {
      return false;
   }
   bool ok = db_exec3("INSERT OR IGNORE INTO networks(name) VALUES(?1)", name, NULL, NULL);

   if (ok) {
      ok = db_exec3("DELETE FROM network_memberships WHERE network_id=" "(SELECT id FROM networks WHERE name=?1)", name, NULL, NULL);
   }
   gchar **tokens = g_strsplit_set(members, ", \t\r\n", -1);

   for (gsize i = 0 ; ok && tokens[i] ; i++) {
      if (!*tokens[i]) {
         continue;
      }
      ok = db_exec3("INSERT OR IGNORE INTO network_memberships(network_id,server_id) "
         "SELECT n.id,s.id FROM networks n JOIN servers s ON s.name=?2 WHERE n.name=?1", name, tokens[i], NULL);
   }

   g_strfreev(tokens);

   if (ok) {
      ok = sqlite3_exec(client_db, "RELEASE network_write", NULL, NULL, NULL) == SQLITE_OK;
   } else {
      sqlite3_exec(client_db, "ROLLBACK TO network_write; RELEASE network_write", NULL, NULL, NULL);
   }

   if (ok) {
      char *key = g_strdup_printf("network:%s.servers", name);
      dict_add(cfg, key, members);
      g_free(key);
   }

   return ok;
}

/* Changes are committed before the compatibility config view is rebuilt. */
bool rrclient_database_server_delete(const char *name) {
   if (!client_db || !name || !*name) {
      return false;
   }

   if (!db_exec3("DELETE FROM servers WHERE name=?1", name, NULL, NULL)) {
      return false;
   }

   return rrclient_database_load_profiles();
}

bool rrclient_database_server_move(const char *name, const char *network) {
   if (!client_db || !name || !*name) {
      return false;
   }

   if (sqlite3_exec(client_db, "SAVEPOINT server_move", NULL, NULL, NULL) != SQLITE_OK) {
      return false;
   }
   bool ok = db_exec3("DELETE FROM network_memberships WHERE server_id=(SELECT id FROM servers WHERE name=?1)", name, NULL, NULL);

   if (ok && network && *network) {
      ok = db_exec3("INSERT INTO network_memberships(network_id,server_id) "
         "SELECT n.id,s.id FROM networks n JOIN servers s ON s.name=?1 WHERE n.name=?2", name, network, NULL);
   }

   if (ok) {
      ok = sqlite3_exec(client_db, "RELEASE server_move", NULL, NULL, NULL) == SQLITE_OK;
   } else {
      sqlite3_exec(client_db, "ROLLBACK TO server_move; RELEASE server_move", NULL, NULL, NULL);
   }

   return ok && rrclient_database_load_profiles();
}

bool rrclient_database_load_profiles(void) {
   if (!client_db || !cfg) {
      return false;
   }
   /* Discard legacy config-file profiles; SQLite is the sole source. */
   GPtrArray *legacy = g_ptr_array_new_with_free_func(g_free);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (g_str_has_prefix(key, "server:") || g_str_has_prefix(key, "network:")) {
         g_ptr_array_add(legacy, g_strdup(key));
      }
   }

   for (gsize i = 0 ; i < legacy->len ; i++) {
      dict_del(cfg, g_ptr_array_index(legacy, i));
   }

   g_ptr_array_free(legacy, TRUE);
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT name,url,username,proxy FROM servers", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   while (sqlite3_step(q) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(q, 0);
      const char *props[] = {
         "server.url", "server.user", "server.proxy"
      };

      for (int i = 0 ; i < 3 ; i++) {
         const char *value = (const char *)sqlite3_column_text(q, i + 1);
         char *key = g_strdup_printf("server:%s.%s", name, props[i]);
         dict_add(cfg, key, value ? value : "");
         g_free(key);
      }
   }
   sqlite3_finalize(q);

   if (sqlite3_prepare_v2(client_db, "SELECT s.name,p.property,p.value FROM server_properties p "
      "JOIN servers s ON s.id=p.server_id", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   while (sqlite3_step(q) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(q, 0);
      const char *property = (const char *)sqlite3_column_text(q, 1);
      const char *value = (const char *)sqlite3_column_text(q, 2);
      char *key = g_strdup_printf("server:%s.%s", name, property);
      dict_add(cfg, key, value);
      g_free(key);
   }
   sqlite3_finalize(q);

   if (sqlite3_prepare_v2(client_db, "SELECT n.name,group_concat(s.name,',') FROM networks n "
      "LEFT JOIN network_memberships m ON m.network_id=n.id " "LEFT JOIN servers s ON s.id=m.server_id GROUP BY n.id", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   while (sqlite3_step(q) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(q, 0);
      const char *members = (const char *)sqlite3_column_text(q, 1);
      char *key = g_strdup_printf("network:%s.servers", name);
      dict_add(cfg, key, members ? members : "");
      g_free(key);
   }
   sqlite3_finalize(q);

   return true;
}

/* Never write the DB-backed compatibility view back into rrclient.cfg. */
bool rrclient_database_config_save(const char *path) {
   if (!cfg || !path) {
      return false;
   }
   dict *copy = dict_new();

   if (!copy || dict_merge(copy, cfg)) {
      dict_free(copy);

      return false;
   }
   GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(copy, rank, &key, &value)) >= 0) {
      if (g_str_has_prefix(key, "server:") || g_str_has_prefix(key, "network:")) {
         g_ptr_array_add(keys, g_strdup(key));
      }
   }

   for (gsize i = 0 ; i < keys->len ; i++) {
      dict_del(copy, g_ptr_array_index(keys, i));
   }

   g_ptr_array_free(keys, TRUE);
   bool ok = cfg_save(copy, path);
   dict_free(copy);

   return ok;
}

/* Named proxies are stored alongside server profiles in client.db. */
bool rrclient_database_proxy_set(const char *name, const char *url) {
   if (!client_db || !name || !*name || !url) return false;
   return db_exec3("INSERT INTO proxies(name,url) VALUES(?1,?2) ON CONFLICT(name) DO UPDATE SET url=excluded.url", name, url, NULL);
}
bool rrclient_database_proxy_delete(const char *name) {
   if (!client_db || !name) return false;
   return db_exec3("DELETE FROM proxies WHERE name=?1", name, NULL, NULL);
}
void rrclient_database_proxy_foreach(void (*cb)(const char *, const char *, void *), void *data) {
   if (!client_db || !cb) return;
   sqlite3_stmt *q = NULL;
   if (sqlite3_prepare_v2(client_db, "SELECT name,url FROM proxies ORDER BY name COLLATE NOCASE", -1, &q, NULL) != SQLITE_OK) return;
   while (sqlite3_step(q) == SQLITE_ROW)
      cb((const char *)sqlite3_column_text(q, 0), (const char *)sqlite3_column_text(q, 1), data);
   sqlite3_finalize(q);
}

/* Database theme definitions override file definitions of the same name/key. */
bool rrclient_database_load_themes(void) {
   if (!client_db || !cfg) return false;
   sqlite3_stmt *q = NULL;
   if (sqlite3_prepare_v2(client_db, "SELECT name,property,value FROM themes ORDER BY name,property", -1, &q, NULL) != SQLITE_OK)
      return false;
   while (sqlite3_step(q) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(q, 0);
      const char *property = (const char *)sqlite3_column_text(q, 1);
      const char *value = (const char *)sqlite3_column_text(q, 2);
      char *key = g_strdup_printf("theme:%s.%s", name, property);
      dict_add(cfg, key, value ? value : "");
      g_free(key);
   }
   sqlite3_finalize(q);
   return true;
}

bool rrclient_database_theme_set(const char *name, const char *property, const char *value) {
   if (!client_db || !name || !*name || !property || !*property || !value ||
       strchr(name, '.') || strchr(name, '\n') || strchr(property, '\n'))
      return false;
   return db_exec3("INSERT INTO themes(name,property,value) VALUES(?1,?2,?3) "
      "ON CONFLICT(name,property) DO UPDATE SET value=excluded.value", name, property, value);
}

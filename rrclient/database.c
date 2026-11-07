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
#include <string.h>
#include <librustyaxe/core.h>
extern dict *cfg;

static sqlite3 *client_db;

static bool window_state_schema(void) {
   if (!client_db) {
      return false;
   }

   /* Window state is an additive migration, independent of older installed SQL schema files. Session id advances once per successful DB open. */
   if (sqlite3_exec(client_db, "CREATE TABLE IF NOT EXISTS audio_profiles ("
      "name TEXT PRIMARY KEY COLLATE NOCASE, updated INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS audio_profile_routes ("
      "profile TEXT NOT NULL REFERENCES audio_profiles(name) ON DELETE CASCADE,"
      "stream_id TEXT NOT NULL, route TEXT NOT NULL,"
      "PRIMARY KEY(profile,stream_id));"
      "CREATE TABLE IF NOT EXISTS window_states ("
      "id TEXT PRIMARY KEY, x INTEGER NOT NULL, y INTEGER NOT NULL,"
      "width INTEGER NOT NULL, height INTEGER NOT NULL, zoom INTEGER NOT NULL,"
      "rolled INTEGER NOT NULL, minimized INTEGER NOT NULL, pinned INTEGER NOT NULL,"
      "last_used INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
      "last_session INTEGER NOT NULL DEFAULT 0);"
      "INSERT OR IGNORE INTO client_settings(key,value) VALUES('window.session','0');"
      "UPDATE client_settings SET value=CAST(value AS INTEGER)+1 WHERE key='window.session';"
      "DELETE FROM window_states WHERE last_session < "
      "(SELECT CAST(value AS INTEGER)-5 FROM client_settings WHERE key='window.session');", NULL, NULL, NULL) != SQLITE_OK) {
      fprintf(stderr, "Window-state table initialization failed: %s\n", sqlite3_errmsg(client_db));

      return false;
   }


   return true;
}

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
      if (sqlite3_exec(client_db,
         "CREATE TABLE IF NOT EXISTS proxies (name TEXT PRIMARY KEY COLLATE NOCASE, url TEXT NOT NULL); CREATE TABLE IF NOT EXISTS themes (name TEXT NOT NULL COLLATE NOCASE, property TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(name,property)); PRAGMA user_version=4",
         NULL, NULL, NULL) != SQLITE_OK) {
         goto fail;
      }

      if (!window_state_schema()) {
         goto fail;
      }
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

   if (!window_state_schema()) {
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
   if (!client_db || !name || !*name || !url) {
      return false;
   }

   return db_exec3("INSERT INTO proxies(name,url) VALUES(?1,?2) ON CONFLICT(name) DO UPDATE SET url=excluded.url", name, url, NULL);
}
bool rrclient_database_proxy_delete(const char *name) {
   if (!client_db || !name) {
      return false;
   }

   return db_exec3("DELETE FROM proxies WHERE name=?1", name, NULL, NULL);
}
void rrclient_database_proxy_foreach(void (*cb) (const char *, const char *, void *), void *data) {
   if (!client_db || !cb) {
      return;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT name,url FROM proxies ORDER BY name COLLATE NOCASE", -1, &q, NULL) != SQLITE_OK) {
      return;
   }
   while (sqlite3_step(q) == SQLITE_ROW) {
      cb((const char *)sqlite3_column_text(q, 0), (const char *)sqlite3_column_text(q, 1), data);
   }
   sqlite3_finalize(q);
}

/* Database theme definitions override file definitions of the same name/key. */
bool rrclient_database_load_themes(void) {
   if (!client_db || !cfg) {
      return false;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT name,property,value FROM themes ORDER BY name,property", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
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
   if (!client_db || !name || !*name || !property || !*property || !value || strchr(name, '.') || strchr(name, '\n') || strchr(property, '\n')) {
      return false;
   }

   return db_exec3("INSERT INTO themes(name,property,value) VALUES(?1,?2,?3) "
      "ON CONFLICT(name,property) DO UPDATE SET value=excluded.value", name, property, value);
}


bool rrclient_database_window_load(const char *id, rrclient_window_state_t *out) {
   if (!client_db || !id || !*id || !out) {
      return false;
   }
   sqlite3_stmt *q = NULL;
   bool found = false;
   const char *sql = "SELECT x,y,width,height,zoom,rolled,minimized,pinned FROM window_states WHERE id=?1";

   if (sqlite3_prepare_v2(client_db, sql, -1, &q, NULL) == SQLITE_OK) {
      sqlite3_bind_text(q, 1, id, -1, SQLITE_TRANSIENT);

      if (sqlite3_step(q) == SQLITE_ROW) {
         *out = (rrclient_window_state_t) {
            .x = sqlite3_column_int(q, 0), .y = sqlite3_column_int(q, 1), .width = sqlite3_column_int(q, 2), .height = sqlite3_column_int(q, 3), .zoom =
               sqlite3_column_int(q, 4), .rolled = sqlite3_column_int(q, 5) != 0, .minimized = sqlite3_column_int(q, 6) != 0, .pinned =
               sqlite3_column_int(q, 7) != 0
         };
         found = true;
      }
   }
   sqlite3_finalize(q);

   return found;
}

bool rrclient_database_window_save(const char *id, const rrclient_window_state_t *state) {
   if (!client_db || !id || !*id || !state) {
      return false;
   }
   sqlite3_stmt *q = NULL;
   const char *sql = "INSERT INTO window_states"
      "(id,x,y,width,height,zoom,rolled,minimized,pinned,last_used,last_session) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,strftime('%s','now'),"
      "(SELECT CAST(value AS INTEGER) FROM client_settings WHERE key='window.session')) "
      "ON CONFLICT(id) DO UPDATE SET "
      "x=excluded.x,y=excluded.y,width=excluded.width,height=excluded.height,"
      "zoom=excluded.zoom,rolled=excluded.rolled,minimized=excluded.minimized,"
      "pinned=excluded.pinned,last_used=excluded.last_used,last_session=excluded.last_session";

   if (sqlite3_prepare_v2(client_db, sql, -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(q, 1, id, -1, SQLITE_TRANSIENT);
   sqlite3_bind_int(q, 2, state->x);
   sqlite3_bind_int(q, 3, state->y);
   sqlite3_bind_int(q, 4, state->width);
   sqlite3_bind_int(q, 5, state->height);
   sqlite3_bind_int(q, 6, state->zoom);
   sqlite3_bind_int(q, 7, state->rolled);
   sqlite3_bind_int(q, 8, state->minimized);
   sqlite3_bind_int(q, 9, state->pinned);
   bool ok = sqlite3_step(q) == SQLITE_DONE;
   sqlite3_finalize(q);

   return ok;
}


bool rrclient_database_audio_profile_save(const char *name, const char *const *ids, const char *routes, size_t count) {
   /* Keep preset names unique case-insensitively. Explicit Save replacement is confirmed in GTK before calling this transactional upsert. */
   if (!client_db || !name || !*name || !g_ascii_strcasecmp(name, "***AUTO***") || !g_ascii_strcasecmp(name, "New Preset") || !ids || !routes || count > 128) {
      return false;
   }

   if (sqlite3_exec(client_db, "SAVEPOINT audio_profile_save", NULL, NULL, NULL) != SQLITE_OK) {
      return false;
   }
   sqlite3_stmt *q = NULL;
   bool ok = false;

   if (sqlite3_prepare_v2(client_db, "INSERT INTO audio_profiles(name,updated) VALUES(?1,strftime('%s','now'))"
      " ON CONFLICT(name) DO UPDATE SET updated=excluded.updated", -1, &q, NULL) != SQLITE_OK) {
      goto done;
   }
   sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
   ok = sqlite3_step(q) == SQLITE_DONE;
   sqlite3_finalize(q);
   q = NULL;

   if (!ok || sqlite3_prepare_v2(client_db, "DELETE FROM audio_profile_routes WHERE profile=?1 COLLATE NOCASE", -1, &q, NULL) != SQLITE_OK) {
      goto done;
   }
   sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
   ok = sqlite3_step(q) == SQLITE_DONE;
   sqlite3_finalize(q);
   q = NULL;

   if (!ok || sqlite3_prepare_v2(client_db, "INSERT INTO audio_profile_routes(profile,stream_id,route) VALUES(?1,?2,?3)", -1, &q, NULL) != SQLITE_OK) {
      goto done;
   }

   for (size_t i = 0 ; i < count ; i++) {
      if (!ids[i] || !*ids[i] || routes[i] == '\0' || !strchr("SLRM", routes[i])) {
         ok = false;
         break;
      }
      char route[2] = {
         routes[i], 0
      };
      sqlite3_reset(q);
      sqlite3_clear_bindings(q);
      sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(q, 2, ids[i], -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(q, 3, route, -1, SQLITE_TRANSIENT);

      if (sqlite3_step(q) != SQLITE_DONE) {
         ok = false;
         break;
      }
   }

done:
   sqlite3_finalize(q);

   if (ok) {
      ok = sqlite3_exec(client_db, "RELEASE audio_profile_save", NULL, NULL, NULL) == SQLITE_OK;
   }

   if (!ok) {
      sqlite3_exec(client_db, "ROLLBACK TO audio_profile_save; RELEASE audio_profile_save", NULL, NULL, NULL);
   }

   return ok;
}
char *rrclient_database_vfo_layout_load(void) {
   if (!client_db) {
      return NULL;
   }
   sqlite3_stmt *q = NULL;
   char *order = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT value FROM client_settings WHERE key='ui.vfo.dock-order'", -1, &q, NULL) == SQLITE_OK && sqlite3_step(q) ==
      SQLITE_ROW) {
      const char *value = (const char *)sqlite3_column_text(q, 0);

      if (value) {
         order = g_strdup(value);
      }
   }
   sqlite3_finalize(q);

   return order;
}
bool rrclient_database_vfo_layout_save(const char *order) {
   if (!client_db || !order) {
      return false;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "INSERT INTO client_settings(key,value) VALUES('ui.vfo.dock-order',?1)"
      " ON CONFLICT(key) DO UPDATE SET value=excluded.value", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(q, 1, order, -1, SQLITE_TRANSIENT);
   bool ok = sqlite3_step(q) == SQLITE_DONE;
   sqlite3_finalize(q);

   return ok;
}

bool rrclient_database_audio_profile_delete(const char *name) {
   if (!client_db || !name || !*name) {
      return false;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "DELETE FROM audio_profiles WHERE name=?1", -1, &q, NULL) != SQLITE_OK) {
      return false;
   }
   sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
   bool ok = sqlite3_step(q) == SQLITE_DONE && sqlite3_changes(client_db) > 0;
   sqlite3_finalize(q);

   return ok;
}

void rrclient_database_audio_profile_list(void (*cb) (const char *, void *), void *user) {
   if (!client_db || !cb) {
      return;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT name FROM audio_profiles ORDER BY name COLLATE NOCASE", -1, &q, NULL) != SQLITE_OK) {
      return;
   }
   while (sqlite3_step(q) == SQLITE_ROW) {
      cb((const char *)sqlite3_column_text(q, 0), user);
   }
   sqlite3_finalize(q);
}
void rrclient_database_audio_profile_load(const char *name, void (*cb) (const char *, char, void *), void *user) {
   if (!client_db || !name || !cb) {
      return;
   }
   sqlite3_stmt *q = NULL;

   if (sqlite3_prepare_v2(client_db, "SELECT stream_id,route FROM audio_profile_routes WHERE profile=?1 COLLATE NOCASE ORDER BY stream_id", -1, &q, NULL) !=
      SQLITE_OK) {
      return;
   }
   sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
   while (sqlite3_step(q) == SQLITE_ROW) {
      const char *id = (const char *)sqlite3_column_text(q, 0);
      const char *route = (const char *)sqlite3_column_text(q, 1);

      if (id && route && route[0] && strchr("SLRM", route[0])) {
         cb(id, route[0], user);
      }
   }
   sqlite3_finalize(q);
}

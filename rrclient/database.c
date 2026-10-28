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
   if (client_db) return true;
   gchar *dir = g_build_filename(g_get_user_state_dir(), "rustyrig", NULL);
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
   if (version == 2) { g_free(path); g_free(dir); return true; }
   if (version != 0 && version != 1) {
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

static bool db_exec3(const char *sql, const char *a, const char *b, const char *c) {
   sqlite3_stmt *stmt = NULL;
   if (!client_db || sqlite3_prepare_v2(client_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
   if (a) sqlite3_bind_text(stmt, 1, a, -1, SQLITE_TRANSIENT);
   if (b) sqlite3_bind_text(stmt, 2, b, -1, SQLITE_TRANSIENT);
   if (c) sqlite3_bind_text(stmt, 3, c, -1, SQLITE_TRANSIENT);
   bool ok = sqlite3_step(stmt) == SQLITE_DONE;
   sqlite3_finalize(stmt);
   return ok;
}

bool rrclient_database_server_set(const char *name, const char *property, const char *value) {
   if (!name || !*name || !property || !value) return false;
   if (sqlite3_exec(client_db, "SAVEPOINT profile_write", NULL, NULL, NULL) != SQLITE_OK) return false;
   bool ok = false;
   if (!strcmp(property, "server.url"))
      ok = db_exec3("INSERT INTO servers(name,url) VALUES(?1,?2) "
         "ON CONFLICT(name) DO UPDATE SET url=excluded.url",name,value,NULL);
   else if (!strcmp(property, "server.user") || !strcmp(property,"server.proxy"))
      ok = db_exec3(!strcmp(property,"server.user") ?
         "UPDATE servers SET username=?2 WHERE name=?1" :
         "UPDATE servers SET proxy=?2 WHERE name=?1",name,value,NULL);
   else ok = db_exec3("INSERT INTO server_properties(server_id,property,value) "
         "SELECT id,?2,?3 FROM servers WHERE name=?1 "
         "ON CONFLICT(server_id,property) DO UPDATE SET value=excluded.value",
         name,property,value);
   if (ok) ok = sqlite3_exec(client_db,"RELEASE profile_write",NULL,NULL,NULL)==SQLITE_OK;
   else sqlite3_exec(client_db,"ROLLBACK TO profile_write; RELEASE profile_write",NULL,NULL,NULL);
   if (ok) {
      char *key=g_strdup_printf("server:%s.%s",name,property);
      dict_add(cfg,key,value);
      g_free(key);
   }
   return ok;
}

bool rrclient_database_network_set(const char *name, const char *members) {
   if (!name || !*name || !members || !client_db) return false;
   if (sqlite3_exec(client_db,"SAVEPOINT network_write",NULL,NULL,NULL)!=SQLITE_OK) return false;
   bool ok=db_exec3("INSERT OR IGNORE INTO networks(name) VALUES(?1)",name,NULL,NULL);
   if (ok) ok=db_exec3("DELETE FROM network_memberships WHERE network_id="
      "(SELECT id FROM networks WHERE name=?1)",name,NULL,NULL);
   gchar **tokens=g_strsplit_set(members,", \t\r\n",-1);
   for (gsize i=0;ok && tokens[i];i++) {
      if (!*tokens[i]) continue;
      ok=db_exec3("INSERT OR IGNORE INTO network_memberships(network_id,server_id) "
         "SELECT n.id,s.id FROM networks n JOIN servers s ON s.name=?2 WHERE n.name=?1",
         name,tokens[i],NULL);
   }
   g_strfreev(tokens);
   if (ok) ok=sqlite3_exec(client_db,"RELEASE network_write",NULL,NULL,NULL)==SQLITE_OK;
   else sqlite3_exec(client_db,"ROLLBACK TO network_write; RELEASE network_write",NULL,NULL,NULL);
   if (ok) {
      char *key=g_strdup_printf("network:%s.servers",name);
      dict_add(cfg,key,members);
      g_free(key);
   }
   return ok;
}

bool rrclient_database_load_profiles(void) {
   if (!client_db || !cfg) return false;
   sqlite3_stmt *q=NULL;
   if (sqlite3_prepare_v2(client_db,"SELECT name,url,username,proxy FROM servers",-1,&q,NULL)!=SQLITE_OK) return false;
   while (sqlite3_step(q)==SQLITE_ROW) {
      const char *name=(const char *)sqlite3_column_text(q,0);
      const char *props[]={"server.url","server.user","server.proxy"};
      for (int i=0;i<3;i++) {
         const char *value=(const char *)sqlite3_column_text(q,i+1);
         char *key=g_strdup_printf("server:%s.%s",name,props[i]);
         dict_add(cfg,key,value?value:"");
         g_free(key);
      }
   }
   sqlite3_finalize(q);
   if (sqlite3_prepare_v2(client_db,"SELECT s.name,p.property,p.value FROM server_properties p "
       "JOIN servers s ON s.id=p.server_id",-1,&q,NULL)!=SQLITE_OK) return false;
   while (sqlite3_step(q)==SQLITE_ROW) {
      const char *name=(const char *)sqlite3_column_text(q,0);
      const char *property=(const char *)sqlite3_column_text(q,1);
      const char *value=(const char *)sqlite3_column_text(q,2);
      char *key=g_strdup_printf("server:%s.%s",name,property);
      dict_add(cfg,key,value);
      g_free(key);
   }
   sqlite3_finalize(q);
   if (sqlite3_prepare_v2(client_db,"SELECT n.name,group_concat(s.name,',') FROM networks n "
      "LEFT JOIN network_memberships m ON m.network_id=n.id "
      "LEFT JOIN servers s ON s.id=m.server_id GROUP BY n.id",-1,&q,NULL)!=SQLITE_OK) return false;
   while (sqlite3_step(q)==SQLITE_ROW) {
      const char *name=(const char *)sqlite3_column_text(q,0);
      const char *members=(const char *)sqlite3_column_text(q,1);
      char *key=g_strdup_printf("network:%s.servers",name);
      dict_add(cfg,key,members?members:"");
      g_free(key);
   }
   sqlite3_finalize(q);
   return true;
}

#include "mesh.bootstrap.h"
#include <glib.h>
#ifdef USE_SQLITE
static char *endpoint(const char *enrollment, unsigned port) {
   GUri *uri = g_uri_parse(enrollment, G_URI_FLAGS_NONE, NULL);
   const char *host = uri ? g_uri_get_host(uri) : NULL;
   char *url = NULL;
   if (host && *host && !g_strcmp0(g_uri_get_scheme(uri), "wss") && port && port <= 65535)
      url = g_strdup_printf(strchr(host, ':') ? "wss://[%s]:%u/link" : "wss://%s:%u/link", host, port);
   if (uri) g_uri_unref(uri);
   return url;
}

bool rr_mesh_bootstrap_store_endpoint(sqlite3 *db, const char *domain,
   const char *node, const char *enrollment, unsigned port) {
   char *url = endpoint(enrollment, port);
   sqlite3_stmt *s = NULL;
   bool ok = url && sqlite3_exec(db,
      "CREATE TABLE IF NOT EXISTS mesh_link_endpoints(domain TEXT NOT NULL, node_uuid TEXT NOT NULL,"
      "url TEXT NOT NULL, PRIMARY KEY(domain,node_uuid))", NULL, NULL, NULL) == SQLITE_OK &&
      sqlite3_prepare_v2(db, "INSERT INTO mesh_link_endpoints VALUES(?1,?2,?3)"
      " ON CONFLICT(domain,node_uuid) DO UPDATE SET url=excluded.url", -1, &s, NULL) == SQLITE_OK;
   if (ok) {
      sqlite3_bind_text(s, 1, domain, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 2, node, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 3, url, -1, SQLITE_TRANSIENT);
      ok = sqlite3_step(s) == SQLITE_DONE;
   }
   sqlite3_finalize(s);
   g_free(url);
   return ok;
}

char *rr_mesh_bootstrap_url(sqlite3 *db, const char *domain, const char *node, const char *enrollment) {
   char *url = NULL;
   sqlite3_stmt *s = NULL;
   if (sqlite3_prepare_v2(db, "SELECT url FROM mesh_link_endpoints WHERE domain=?1 AND node_uuid=?2", -1, &s, NULL) == SQLITE_OK) {
      sqlite3_bind_text(s, 1, domain, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, 2, node, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(s) == SQLITE_ROW) url = g_strdup((const char *)sqlite3_column_text(s, 0));
   }
   sqlite3_finalize(s);
   /* Upgrade existing enrollments without ever treating 4420 as mesh WSS. */
   return url ? url : endpoint(enrollment, 4421);
}
#endif

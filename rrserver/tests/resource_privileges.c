/* Scoped resource privilege evaluator regression checks. */
#include <assert.h>
#include <stdio.h>
#include <sqlite3.h>
#include <rrserver/database.h>
static void run(sqlite3 *db, const char *sql) {
   char *error = NULL;
   int rc = sqlite3_exec(db, sql, NULL, NULL, &error);

   if (rc != SQLITE_OK) {
      fprintf(stderr, "SQL: %s: %s\n", sql, error ? error : sqlite3_errmsg(db));
      sqlite3_free(error);
   }
   assert(rc == SQLITE_OK);
}

#define CHECK(user, host, priv, t) assert(db_resource_access(db, user, host, "rig", "12345678-1234-4234-8234-1234567890ab", priv, t) == RR_RESOURCE_ALLOW)
#define DENY(user, host, priv, t) assert(db_resource_access(db, user, host, "rig", "12345678-1234-4234-8234-1234567890ab", priv, t) == RR_RESOURCE_DENY)
int main(void) {
   sqlite3 *db = NULL;
   assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
   run(db, "PRAGMA foreign_keys=ON;"
      "CREATE TABLE resource_privileges(subject_user TEXT,subject_host TEXT,resource_kind TEXT,resource_uuid TEXT,privilege TEXT,effect TEXT,expires_at INTEGER);"
      "CREATE TABLE privilege_sets(id INTEGER PRIMARY KEY,name TEXT);"
      "CREATE TABLE privilege_set_entries(set_id INTEGER,privilege TEXT,effect TEXT);"
      "CREATE TABLE privilege_set_assignments(set_id INTEGER,subject_user TEXT,subject_host TEXT,resource_kind TEXT,resource_uuid TEXT,expires_at INTEGER);");
   const char *rig = "12345678-1234-4234-8234-1234567890ab";
   assert(db_resource_access(db, "alice", "camp", "rig", rig, "rig.use", 100) == RR_RESOURCE_UNSET);
   run(db, "INSERT INTO privilege_sets VALUES(1,'hf-listen');"
      "INSERT INTO privilege_set_entries VALUES(1,'rig.use','allow');"
      "INSERT INTO privilege_set_assignments VALUES(1,'alice','camp','rig','12345678-1234-4234-8234-1234567890ab',NULL);");
   CHECK("alice", "camp", "rig.use", 100);
   assert(db_resource_access(db, "alice", "wrong-site", "rig", rig, "rig.use", 100) == RR_RESOURCE_UNSET);
   assert(db_resource_access(db, "mallory", "camp", "rig", rig, "rig.use", 100) == RR_RESOURCE_UNSET);
   assert(db_resource_access(db, "alice", "camp", "rig", rig, "tx", 100) == RR_RESOURCE_UNSET);
   run(db, "INSERT INTO resource_privileges VALUES('alice','camp','rig','12345678-1234-4234-8234-1234567890ab','rig.use','deny',NULL);");
   DENY("alice", "camp", "rig.use", 100);
   run(db, "DELETE FROM resource_privileges;" "UPDATE privilege_set_assignments SET expires_at=100;");
   assert(db_resource_access(db, "alice", "camp", "rig", rig, "rig.use", 100) == RR_RESOURCE_UNSET);
   CHECK("alice", "camp", "rig.use", 99);
   run(db, "UPDATE privilege_set_assignments SET expires_at=NULL;" "INSERT INTO privilege_set_entries VALUES(1,'tx','deny');");
   DENY("alice", "camp", "tx", 99);
   run(db, "INSERT INTO resource_privileges VALUES('alice','camp','rig','12345678-1234-4234-8234-1234567890ab','tx','allow',NULL);");
   DENY("alice", "camp", "tx", 99);
   assert(db_resource_access(db, NULL, "camp", "rig", rig, "rig.use", 99) == RR_RESOURCE_DENY);
   sqlite3_close(db);
   puts("resource privilege grants and deny precedence: OK");

   return 0;
}

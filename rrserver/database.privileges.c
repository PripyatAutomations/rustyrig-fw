//
// rrserver/database.privileges.c: scoped resource grant evaluation
// This is part of rustyrig-fw, licensed under MIT without Mongoose.
//
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sqlite3.h>
#include <librustyaxe/core.h>
#include <rrserver/database.h>

#ifdef USE_SQLITE
/*
 * A subject is an already authenticated, authoritative user@host name.
 * This function MUST NOT perform remote identity resolution or accept
 * client-supplied identity fields. Caller resolves the authenticated user.
 *
 * Evaluate direct grants and privilege-set membership together.
 * Explicit deny beats allow, across both sources. No matching rule denies.
 * This deliberately does NOT fall back to users.permissions; core privileges
 * and resource authorization are separate security domains.
 */
rr_resource_access_t db_resource_access_named(sqlite3 *db,
      const char *subject_user, const char *subject_host,
      const char *resource_kind, const char *resource_uuid,
      const char *privilege, time_t now, const char *symbolic_name) {
   if (!db || !subject_user || !*subject_user || !subject_host ||
       !resource_kind || !*resource_kind || !resource_uuid ||
       !*resource_uuid || !privilege || !*privilege)
      return RR_RESOURCE_DENY;

   static const char sql[] =
      "SELECT effect FROM resource_privileges "
      "WHERE subject_user=?1 COLLATE NOCASE AND subject_host=?2 COLLATE NOCASE "
      "AND resource_kind=?3 AND (resource_uuid=?4 OR (resource_kind IN (\'rig\',\'room\') AND resource_uuid=\'*\') OR (length(?7)>0 AND resource_uuid=?7 COLLATE NOCASE)) AND (privilege=?5 OR (resource_kind=\'rig\' AND privilege=\'rig.full\')) "
      "AND (expires_at IS NULL OR expires_at>?6) "
      "UNION ALL "
      "SELECT e.effect FROM privilege_set_assignments AS a "
      "JOIN privilege_set_entries AS e ON e.set_id=a.set_id "
      "WHERE a.subject_user=?1 COLLATE NOCASE AND a.subject_host=?2 COLLATE NOCASE "
      "AND a.resource_kind=?3 AND (a.resource_uuid=?4 OR (a.resource_kind IN (\'rig\',\'room\') AND a.resource_uuid=\'*\') OR (length(?7)>0 AND a.resource_uuid=?7 COLLATE NOCASE)) AND (e.privilege=?5 OR (a.resource_kind=\'rig\' AND e.privilege=\'rig.full\')) "
      "AND (a.expires_at IS NULL OR a.expires_at>?6);";
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
      return RR_RESOURCE_DENY;
   int rc = sqlite3_bind_text(stmt, 1, subject_user, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, subject_host, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, resource_kind, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, resource_uuid, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 5, privilege, -1, SQLITE_TRANSIENT);
   if (rc == SQLITE_OK) rc = sqlite3_bind_int64(stmt, 6, (sqlite3_int64)now);
   if (rc == SQLITE_OK) rc=sqlite3_bind_text(stmt,7,symbolic_name ? symbolic_name : "",-1,SQLITE_TRANSIENT);
   if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return RR_RESOURCE_DENY; }

   bool allow = false;
   while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const unsigned char *effect = sqlite3_column_text(stmt, 0);
      if (effect && strcmp((const char *)effect, "deny") == 0) {
         sqlite3_finalize(stmt);
         return RR_RESOURCE_DENY;
      }
      if (effect && strcmp((const char *)effect, "allow") == 0) allow = true;
   }
   sqlite3_finalize(stmt);
   if (rc != SQLITE_DONE) return RR_RESOURCE_DENY;
   return allow ? RR_RESOURCE_ALLOW : RR_RESOURCE_UNSET;
}
rr_resource_access_t db_resource_access(sqlite3 *db,const char *subject_user,
   const char *subject_host,const char *resource_kind,const char *resource_uuid,
   const char *privilege,time_t now) {
   return db_resource_access_named(db,subject_user,subject_host,resource_kind,
      resource_uuid,privilege,now,NULL);
}
#endif

/* One-shot rrserver maintenance. Included by main.c to avoid a separate
 * executable and ensure the same effective configuration is used. */
#ifndef RR_SERVER_MAINTENANCE_H
#define RR_SERVER_MAINTENANCE_H
#include <glib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif
#include <limits.h>
#include <strings.h>
#include <librustyaxe/config.h>

/* Cooperating v1 servers and maintenance actions hold this lock for the
 * process lifetime. Do not unlink the lock file: doing so breaks exclusion. */
static int rr_maintenance_lock_fd = -1;
static bool rrserver_maintenance_lock(void) {
   if (rr_maintenance_lock_fd >= 0) return true;
   char *dbpath = cfg_get_path("path.db.master");
   if (!dbpath) dbpath = strdup(MASTERDB_PATH);
   if (!dbpath) return false;
   size_t n = strlen(dbpath) + sizeof(".rrlock");
   char *lockpath = malloc(n);
   if (!lockpath) { free(dbpath); return false; }
   snprintf(lockpath, n, "%s.rrlock", dbpath);
   int fd = open(lockpath, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
   if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
      fprintf(stderr, "Cannot lock database at %s (another rrserver may be active): %s\n",
         dbpath, strerror(errno));
      if (fd >= 0) close(fd);
      free(lockpath);
      free(dbpath);
      return false;
   }
   rr_maintenance_lock_fd = fd;
   free(lockpath);
   free(dbpath);
   return true;
}

enum rr_maintenance_action {
   RR_MAINT_NONE, RR_MAINT_PURGE_RECORDINGS, RR_MAINT_RESET_DB,
   RR_MAINT_RESET_ROOMS, RR_MAINT_RESET_LOGS, RR_MAINT_RESET_AUDIT,
   RR_MAINT_RESET_ADMINPW, RR_MAINT_RESET_UUIDS
};

static const char *rr_maintenance_name(enum rr_maintenance_action action) {
   switch (action) {
      case RR_MAINT_PURGE_RECORDINGS: return "--purge-recordings";
      case RR_MAINT_RESET_DB: return "--reset-db";
      case RR_MAINT_RESET_ROOMS: return "--reset-rooms";
      case RR_MAINT_RESET_LOGS: return "--reset-logs";
      case RR_MAINT_RESET_AUDIT: return "--reset-audit";
      case RR_MAINT_RESET_ADMINPW: return "--reset-adminpw";
      case RR_MAINT_RESET_UUIDS: return "--reset-uuids";
      default: return "unknown";
   }
}

static bool rr_maintenance_confirm(enum rr_maintenance_action action, const char *target) {
   FILE *terminal = fopen("/dev/tty", "r+");
   if (!terminal) {
      fprintf(stderr, "%s requires an interactive terminal; no changes made.\n",
         rr_maintenance_name(action));
      return false;
   }
   fprintf(terminal,
      "\nDANGER: %s\nTarget: %s\n"
      "Stop rrserver and back up your data before continuing.\n"
      "Enter exactly 'YES %s' to proceed: ",
      rr_maintenance_name(action), target, rr_maintenance_name(action));
   fflush(terminal);
   char answer[128] = {0}, expected[96];
   snprintf(expected, sizeof(expected), "YES %s", rr_maintenance_name(action));
   bool confirmed = fgets(answer, sizeof(answer), terminal) != NULL;
   if (confirmed) {
      size_t len = strlen(answer);
      confirmed = len > 0 && answer[len - 1] == '\n';
      answer[strcspn(answer, "\r\n")] = 0;
      confirmed = confirmed && !strcmp(answer, expected);
   }
   fclose(terminal);
   if (!confirmed) fprintf(stderr, "Cancelled; no changes made.\n");
   return confirmed;
}

/* Purge known audio recording formats, not arbitrary files in the configured
 * directory. Walk by fd without following symlinks; preserve directories. */
static bool rr_recording_suffix(const char *name) {
   const char *dot = strrchr(name, '.');
   if (!dot) return false;
   const char *extensions[] = { ".flac", ".wav", ".ogg", ".opus", ".mp3", ".aac", ".m4a", ".pcm", ".raw" };
   for (unsigned i = 0; i < sizeof(extensions)/sizeof(extensions[0]); ++i)
      if (!strcasecmp(dot, extensions[i])) return true;
   return false;
}

static bool rr_purge_recording_tree(int fd, unsigned depth, unsigned *removed) {
   if (depth > 16) return false;
   DIR *directory = fdopendir(dup(fd));
   if (!directory) return false;
   bool ok = true;
   struct dirent *entry;
   while ((entry = readdir(directory))) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      struct stat st;
      if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
      if (S_ISDIR(st.st_mode)) {
         int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
         if (child < 0) { ok = false; break; }
         bool child_ok = rr_purge_recording_tree(child, depth + 1, removed);
         close(child);
         if (!child_ok) { ok = false; break; }
      } else if (S_ISREG(st.st_mode) && rr_recording_suffix(entry->d_name)) {
         if (unlinkat(fd, entry->d_name, 0) != 0) { ok = false; break; }
         ++*removed;
      }
   }
   closedir(directory);
   return ok;
}

#ifdef USE_SQLITE
static bool rr_maintenance_sql(sqlite3 *db, const char *sql) {
   char *error = NULL;
   int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
   if (rc != SQLITE_OK) fprintf(stderr, "Database maintenance failed: %s\n",
      error ? error : sqlite3_errmsg(db));
   sqlite3_free(error);
   return rc == SQLITE_OK;
}

/* Initial provisioning helper lives in database.c so normal bootstrap and
 * explicit credential reset share the same generation and hashing code. */
extern bool db_reset_admin_password(sqlite3 *db);

#endif

static int rrserver_maintenance_run(enum rr_maintenance_action action) {
   const bool recordings = action == RR_MAINT_PURGE_RECORDINGS;
   char *path = cfg_get_path(recordings ? "fwdsp:recording.path" : "path.db.master");
   if (!path || !*path) {
      fprintf(stderr, "No configured %s path; refusing maintenance.\n",
         recordings ? "recording" : "database");
      free(path);
      return EXIT_FAILURE;
   }
   char *resolved = realpath(path, NULL);
   if (!resolved || !strcmp(resolved, "/")) {
      fprintf(stderr, "Maintenance target must exist and must not be filesystem root: %s\n", path);
      free(path);
      free(resolved);
      return EXIT_FAILURE;
   }
   if (!rrserver_maintenance_lock() || !rr_maintenance_confirm(action, resolved)) {
      free(resolved);
      free(path);
      return EXIT_FAILURE;
   }
   int result = EXIT_FAILURE;
   if (recordings) {
      int dir = open(resolved, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (dir >= 0) {
         unsigned removed = 0;
         if (rr_purge_recording_tree(dir, 0, &removed)) {
            fprintf(stderr, "Removed %u audio recording files.\n", removed);
            result = EXIT_SUCCESS;
         } else perror("purge recordings");
         close(dir);
      } else perror("open recording directory");
   } else {
#ifdef USE_SQLITE
      sqlite3 *db = NULL;
      if (sqlite3_open_v2(resolved, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
         fprintf(stderr, "Cannot open existing database %s: %s\n", resolved,
            db ? sqlite3_errmsg(db) : "unknown error");
         if (db) sqlite3_close(db);
      } else {
         sqlite3_busy_timeout(db, 1000);
         if (action == RR_MAINT_RESET_DB) {
            /* Check exclusive access and checkpoint WAL before unlinking. */
            bool ok = rr_maintenance_sql(db, "PRAGMA locking_mode=EXCLUSIVE;") &&
               rr_maintenance_sql(db, "BEGIN EXCLUSIVE;") &&
               rr_maintenance_sql(db, "COMMIT;") &&
               rr_maintenance_sql(db, "PRAGMA wal_checkpoint(TRUNCATE);");
            if (sqlite3_close(db) != SQLITE_OK) ok = false;
            db = NULL;
            if (ok) {
               char *wal = g_strdup_printf("%s-wal", resolved);
               char *shm = g_strdup_printf("%s-shm", resolved);
               char *bootstrap = g_strdup_printf("%s.bootstrap-password", resolved);
               bool cleaned = wal && shm && bootstrap;
               if (cleaned && unlink(wal) != 0 && errno != ENOENT) cleaned = false;
               if (cleaned && unlink(shm) != 0 && errno != ENOENT) cleaned = false;
               if (cleaned && unlink(resolved) == 0) {
                  if (unlink(bootstrap) != 0 && errno != ENOENT)
                     fprintf(stderr, "Warning: could not remove old bootstrap credential %s\\n", bootstrap);
                  result = EXIT_SUCCESS;
               } else {
                  perror("reset database");
               }
               g_free(wal);
               g_free(shm);
               g_free(bootstrap);
            }
         } else {
            const char *sql = NULL;
            switch (action) {
               case RR_MAINT_RESET_ROOMS:
                  sql = "DELETE FROM room_vfos; DELETE FROM rooms;"; break;
               case RR_MAINT_RESET_LOGS:
                  sql = "DELETE FROM chat_log; DELETE FROM ptt_log;"; break;
               case RR_MAINT_RESET_AUDIT:
                  sql = "DELETE FROM audit_log;"; break;
               case RR_MAINT_RESET_UUIDS:
                  sql = "DELETE FROM room_vfos; DELETE FROM vfo_identities; DELETE FROM rig_identities;"; break;
               default: break;
            }
            bool ok = rr_maintenance_sql(db, "BEGIN IMMEDIATE;");
            if (ok) ok = action == RR_MAINT_RESET_ADMINPW ?
               db_reset_admin_password(db) : (sql && rr_maintenance_sql(db, sql));
            if (ok && action == RR_MAINT_RESET_UUIDS)
               ok = db_room_uuids_regenerate(db);
            if (ok) ok = rr_maintenance_sql(db, "COMMIT;");
            if (!ok) rr_maintenance_sql(db, "ROLLBACK;");
            if (ok) result = EXIT_SUCCESS;
            sqlite3_close(db);
         }
      }
#else
      fprintf(stderr, "Database maintenance requires a SQLite-enabled build.\n");
#endif
   }
   if (result == EXIT_SUCCESS)
      fprintf(stderr, "Completed %s. Server was not started.\n", rr_maintenance_name(action));
   free(resolved);
   free(path);
   return result;
}
#endif

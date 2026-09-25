// SQLite integration coverage for the server persistence helpers.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sqlite3.h>
#include <librrprotocol/auth.h>
#include <rrserver/database.h>

time_t now;
bool dying;
bool restarting;

static void run_file(sqlite3 *db, const char *path) {
   FILE *fp = fopen(path, "rb");
   assert(fp);
   assert(fseek(fp, 0, SEEK_END) == 0);
   long size = ftell(fp);
   assert(size >= 0);
   rewind(fp);
   char *sql = calloc(1, (size_t)size + 1);
   assert(sql);
   assert(fread(sql, 1, (size_t)size, fp) == (size_t)size);
   fclose(fp);
   char *error = NULL;
   assert(sqlite3_exec(db, sql, NULL, NULL, &error) == SQLITE_OK);
   sqlite3_free(error);
   free(sql);
}

static int count_rows(sqlite3 *db, const char *sql) {
   sqlite3_stmt *stmt = NULL;
   assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
   assert(sqlite3_step(stmt) == SQLITE_ROW);
   int count = sqlite3_column_int(stmt, 0);
   sqlite3_finalize(stmt);
   return count;
}

int main(void) {
   sqlite3 *db = NULL;
   assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
   masterdb = db;
   run_file(db, "sql/sqlite.master.sql");
   run_file(db, "sql/sqlite.master.preload.sql");

   assert(db_room_ensure(db, "#alpha", true, 3));
   assert(db_room_set_topic(db, "#alpha", "Test topic"));
   char *topic = db_room_get_topic(db, "#alpha");
   assert(topic && strcmp(topic, "Test topic") == 0);
   free(topic);
   assert(db_room_vfo_add(db, "#alpha", "rig0.vfo_a"));
   assert(db_room_vfo_add(db, "#alpha", "rig0.vfo_b"));
   assert(db_room_vfo_add(db, "#alpha", "rig0.vfo_a")); // idempotent
   char *vfos = db_room_vfo_list(db, "#alpha");
   assert(vfos && strcmp(vfos, "rig0.vfo_a rig0.vfo_b") == 0);
   free(vfos);
   char *rooms = db_room_list(db);
   assert(rooms && strstr(rooms, "#alpha"));
   free(rooms);
   assert(db_room_ensure(db, "#beta", true, 0));
   assert(db_room_vfo_add(db, "#beta", "rig1.vfo_a"));
   char *mapping = db_room_vfo_map_list(db);
   assert(mapping && strcmp(mapping, "#alpha: rig0.vfo_a, rig0.vfo_b\n#beta: rig1.vfo_a") == 0);
   free(mapping);
   assert(db_room_vfo_remove(db, "#alpha", "rig0.vfo_b"));
   assert(db_room_delete(db, "#alpha"));
   assert(count_rows(db, "SELECT COUNT(*) FROM rooms WHERE name='#alpha';") == 0);
   assert(count_rows(db, "SELECT COUNT(*) FROM room_vfos WHERE room='#alpha';") == 0);

   assert(db_add_user(db, 9, "test-user", true, "hash", "test@example.invalid", 2, "view"));
   assert(!db_add_user(db, 10, NULL, true, "hash", "x", 1, "view"));
   assert(db_get_users(db) == 4);
   assert(strcmp(http_users[9].name, "test-user") == 0);
   assert(http_users[9].max_sessions == 2);

   int session = db_ptt_start(db, "test-user", "A", 14074000, "USB", 3000, 25.0f,
      "/tmp/20260923.rec-123.test-user.tx.ogg", "rec-123");
   assert(session > 0);
   int duration = -1;
   assert(db_ptt_stop(db, session, &duration, "timeout"));
   assert(duration >= 0);
   assert(count_rows(db, "SELECT COUNT(*) FROM ptt_log WHERE recording_id='rec-123' AND record_file LIKE '%rec-123%' AND stop_reason='timeout' AND end_time IS NOT NULL;") == 1);

   assert(db_add_chat_msg(db, 1234, "test-user", "#room", "pub", "hello"));
   assert(count_rows(db, "SELECT COUNT(*) FROM chat_log WHERE msg_data='hello';") == 1);
   assert(db_add_audit_event(db, "test-user", "test", "details"));
   assert(count_rows(db, "SELECT COUNT(*) FROM audit_log WHERE event_type='test';") == 1);

   assert(db_quota_get(db, "admin") == 14400);
   assert(db_quota_spend(db, "admin", 60));
   assert(db_quota_get(db, "admin") == 14340);
   assert(db_quota_add(db, "new-user", 10));
   assert(db_quota_set(db, "new-user", 25));
   assert(db_quota_get(db, "new-user") == 25);

   sqlite3_close(db);
   puts("PASS: database schema, rooms, users, PTT recordings, chat, audit, and quota");
   return 0;
}

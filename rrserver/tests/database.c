// SQLite integration coverage for the server persistence helpers.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sqlite3.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/auth.h>
#include <rrserver/database.h>

time_t now;
bool dying;
bool restarting;

#define MAX_REPLAY_FRAMES 64
struct replay_frame {
   char cmd[32];
   char type[32];
   char target[64];
   char data[64];
};
static struct replay_frame replay_frames[MAX_REPLAY_FRAMES];
static int replay_frame_count;

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int data_type) {
   (void)sender;
   (void)data_type;
   assert(dest);
   assert(d);
   assert(replay_frame_count < MAX_REPLAY_FRAMES);
   struct replay_frame *frame = &replay_frames[replay_frame_count++];
   snprintf(frame->cmd, sizeof(frame->cmd), "%s", dict_get(d, "talk.cmd", ""));
   snprintf(frame->type, sizeof(frame->type), "%s", dict_get(d, "talk.msg_type", ""));
   snprintf(frame->target, sizeof(frame->target), "%s", dict_get(d, "talk.target", ""));
   snprintf(frame->data, sizeof(frame->data), "%s", dict_get(d, "talk.data", ""));
   return true;
}

static void reset_replay_frames(void) {
   memset(replay_frames, 0, sizeof(replay_frames));
   replay_frame_count = 0;
}

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
   now = time(NULL);
   cfg = dict_new();
   default_cfg = dict_new();
   assert(cfg && default_cfg);
   dict_add(default_cfg, "chat.replay-lines", "20");
   dict_add(cfg, "chat.replay-lines", "20");
   assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
   masterdb = db;
   run_file(db, "sql/sqlite.master.sql");
   run_file(db, "sql/sqlite.master.preload.sql");

   // Public persistence helpers must reject incomplete calls without
   // dereferencing a null handle or inserting malformed state.
   assert(!db_room_ensure(NULL, "#invalid", false, 0));
   assert(!db_room_ensure(db, NULL, false, 0));
   assert(!db_room_ensure(db, "", false, 0));
   assert(!db_room_set_topic(db, "#missing", NULL));
   assert(db_room_get_topic(db, "#missing") == NULL);
   assert(!db_room_vfo_add(db, NULL, "rig0.vfo_a"));
   assert(!db_room_vfo_add(db, "#missing", NULL));
   assert(!db_room_vfo_remove(db, NULL, "rig0.vfo_a"));
   assert(db_get_users(NULL) == -1);
   assert(db_ptt_start(NULL, "admin", "A", 14074000, "USB", 3000, 25.0f, NULL, "id") == -1);
   assert(!db_ptt_stop(db, -1, NULL, NULL));
   assert(db_quota_get(NULL, "admin") == -1);
   assert(!db_quota_spend(db, "admin", 0));
   assert(!db_quota_spend(db, NULL, 1));

   char *rig0_uuid = db_rig_uuid_get_or_create(db, "node-a", "rig0");
   char *rig0_uuid_again = db_rig_uuid_get_or_create(db, "node-a", "rig0");
   char *rig1_uuid = db_rig_uuid_get_or_create(db, "node-a", "rig1");
   char *other_node_uuid = db_rig_uuid_get_or_create(db, "node-b", "rig0");
   assert(rig0_uuid && rig0_uuid_again && rig1_uuid && other_node_uuid);
   assert(g_uuid_string_is_valid(rig0_uuid));
   assert(g_uuid_string_is_valid(rig1_uuid));
   assert(g_uuid_string_is_valid(other_node_uuid));
   assert(strcmp(rig0_uuid, rig0_uuid_again) == 0);
   assert(strcmp(rig0_uuid, rig1_uuid) != 0);
   assert(strcmp(rig0_uuid, other_node_uuid) != 0);
   assert(!db_rig_uuid_get_or_create(db, "", "rig0"));
   assert(count_rows(db, "SELECT COUNT(*) FROM rig_identities;") == 3);

   char *vfo_a = db_vfo_uuid_get_or_create(db, rig0_uuid, "A");
   char *vfo_a_again = db_vfo_uuid_get_or_create(db, rig0_uuid, "A");
   char *vfo_b = db_vfo_uuid_get_or_create(db, rig0_uuid, "B");
   char *rig1_vfo_a = db_vfo_uuid_get_or_create(db, rig1_uuid, "A");
   assert(vfo_a && vfo_a_again && vfo_b && rig1_vfo_a);
   assert(g_uuid_string_is_valid(vfo_a));
   assert(strcmp(vfo_a, vfo_a_again) == 0);
   assert(strcmp(vfo_a, vfo_b) != 0);
   assert(strcmp(vfo_a, rig1_vfo_a) != 0);
   assert(!db_vfo_uuid_get_or_create(db, rig0_uuid, ""));
   assert(count_rows(db, "SELECT COUNT(*) FROM vfo_identities;") == 3);
   gchar *ephemeral_uuid = g_uuid_string_random();
   assert(ephemeral_uuid && g_uuid_string_is_valid(ephemeral_uuid));
   /* Ephemeral allocation deliberately never calls the persistence helper. */
   assert(count_rows(db, "SELECT COUNT(*) FROM vfo_identities;") == 3);
   g_free(ephemeral_uuid);
   free(vfo_a);
   free(vfo_a_again);
   free(vfo_b);
   free(rig1_vfo_a);

   /* Simulate a process restart by closing and reopening a file-backed DB. */
   char restart_path[] = "/tmp/rustyrig-vfo-identity-XXXXXX";
   int restart_fd = mkstemp(restart_path);
   assert(restart_fd >= 0);
   close(restart_fd);
   sqlite3 *restart_db = NULL;
   assert(sqlite3_open(restart_path, &restart_db) == SQLITE_OK);
   run_file(restart_db, "sql/sqlite.master.sql");
   char *restart_rig = db_rig_uuid_get_or_create(restart_db,
      "restart-node", "rig0");
   char *before_restart = db_vfo_uuid_get_or_create(restart_db,
      restart_rig, "A");
   assert(restart_rig && before_restart);
   sqlite3_close(restart_db);
   restart_db = NULL;
   assert(sqlite3_open(restart_path, &restart_db) == SQLITE_OK);
   char *after_restart = db_vfo_uuid_get_or_create(restart_db,
      restart_rig, "A");
   assert(after_restart && strcmp(before_restart, after_restart) == 0);
   sqlite3_close(restart_db);
   unlink(restart_path);
   free(restart_rig);
   free(before_restart);
   free(after_restart);

   free(rig0_uuid);
   free(rig0_uuid_again);
   free(rig1_uuid);
   free(other_node_uuid);

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
   assert(db_get_users(db) == 3);
   assert(http_getuid("bob") < 0);
   assert(http_getuid("guest") == 2);
   assert(!http_users[2].enabled);
   assert(http_users[2].password_set > 0);
   assert(strcmp(http_users[9].name, "test-user") == 0);
   assert(http_users[9].max_sessions == 2);
   assert(http_users[9].password_set > 0);

   assert(db_user_next_uid(db) == 10);
   assert(db_user_create(db, 10, "new-user", true, "new-hash", "new@example.invalid",
      1, "view,chat", true, now + 7 * 86400));
   assert(db_get_users(db) == 4);
   assert(http_users[10].password_change_required);
   assert(http_users[10].password_expires > now);
   assert(db_user_set_privileges(db, "new-user", "view,chat,radio"));
   assert(db_get_users(db) == 4);
   assert(strcmp(http_users[10].privs, "view,chat,radio") == 0);
   assert(db_user_set_enabled(db, "new-user", false));
   assert(db_user_update_password(db, "new-user", "updated-hash", false, 0));
   assert(db_get_users(db) == 4);
   assert(!http_users[10].enabled);
   assert(!http_users[10].password_change_required);
   assert(http_users[10].password_expires == 0);

   // Reloads must retain runtime session/mute state for connected users.
   http_users[1].sessions = 2;
   http_users[1].is_muted = 1;
   assert(db_get_users(db) == 4);
   assert(http_users[1].sessions == 2);
   assert(http_users[1].is_muted == 1);
   assert(db_quota_add(db, "new-user", 30));
   assert(!db_user_remove(db, "missing-user"));
   assert(db_user_remove(db, "new-user"));
   assert(db_get_users(db) == 3);
   assert(count_rows(db, "SELECT COUNT(*) FROM tx_credits WHERE name='new-user';") == 0);

   int session = db_ptt_start(db, "test-user", "A", 14074000, "USB", 3000, 25.0f,
      "/tmp/20260923.rec-123.test-user.tx.ogg", "rec-123");
   assert(session > 0);
   int duration = -1;
   assert(db_ptt_stop(db, session, &duration, "timeout"));
   assert(duration >= 0);
   assert(count_rows(db, "SELECT COUNT(*) FROM ptt_log WHERE recording_id='rec-123' AND record_file LIKE '%rec-123%' AND stop_reason='timeout' AND end_time IS NOT NULL;") == 1);

   assert(db_add_chat_msg(db, 1234, "test-user", "#room", "pub", "hello"));
   assert(count_rows(db, "SELECT COUNT(*) FROM chat_log WHERE msg_data='hello';") == 1);

   for (int i = 0; i < 30; i++) {
      char line[32];
      snprintf(line, sizeof(line), "line-%02d", i);
      assert(db_add_chat_msg(db, 2000 + i, "test-user", "#room", "pub", line));
   }
   rrconn_t replay_client = {0};
   reset_replay_frames();
   assert(db_send_chat_replay(&replay_client, "#room"));
   assert(replay_frame_count == 22); // start + configured 20 lines + complete
   assert(strcmp(replay_frames[0].cmd, "replay-start") == 0);
   assert(strcmp(replay_frames[1].data, "line-10") == 0);
   assert(strcmp(replay_frames[20].data, "line-29") == 0);
   assert(strcmp(replay_frames[20].type, "replay-pub") == 0);
   assert(strcmp(replay_frames[21].cmd, "replay-complete") == 0);

   dict_add(cfg, "chat.replay-lines", "3");
   reset_replay_frames();
   assert(db_send_chat_replay(&replay_client, "#room"));
   assert(replay_frame_count == 5);
   assert(strcmp(replay_frames[1].data, "line-27") == 0);
   assert(strcmp(replay_frames[3].data, "line-29") == 0);

   dict_add(cfg, "chat.replay-lines", "0");
   reset_replay_frames();
   assert(db_send_chat_replay(&replay_client, "#room"));
   assert(replay_frame_count == 0);

   assert(db_add_audit_event(db, "test-user", "test", "details"));
   assert(count_rows(db, "SELECT COUNT(*) FROM audit_log WHERE event_type='test';") == 1);

   assert(db_quota_get(db, "admin") == 14400);
   assert(db_quota_spend(db, "admin", 60));
   assert(db_quota_get(db, "admin") == 14340);
   assert(db_quota_add(db, "new-user", 10));
   assert(db_quota_set(db, "new-user", 25));
   assert(db_quota_get(db, "new-user") == 25);

   sqlite3_close(db);
   dict_free(cfg);
   cfg = NULL;
   dict_free(default_cfg);
   default_cfg = NULL;
   puts("PASS: database schema, persistent rig/VFO UUID identities, rooms, users, PTT recordings, bounded chat replay, audit, and quota");
   return 0;
}

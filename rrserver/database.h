//
// rrserver/database.c: sqlite3 database stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrserver_database_h)
#define	__rrserver_database_h
#include <stdbool.h>
#include <time.h>
#if     defined(USE_SQLITE)
#include <sqlite3.h>

extern sqlite3 *db_open(const char *path);
extern bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled, const char *password, const char *email,
                        int maxsessions, const char *permissions);
extern bool db_user_create(sqlite3 *db, int uid, const char *name, bool enabled, const char *password,
                           const char *email, int maxsessions, const char *permissions, bool password_change_required,
                           time_t password_expires);
extern bool db_user_set_enabled(sqlite3 *db, const char *name, bool enabled);
extern bool db_user_set_privileges(sqlite3 *db, const char *name, const char *privileges);
extern bool db_user_remove(sqlite3 *db, const char *name);
extern bool db_user_update_password(sqlite3 *db, const char *name, const char *password_hash,
                                    bool password_change_required, time_t password_expires);
extern int db_user_next_uid(sqlite3 *db);
extern int db_get_users(sqlite3 *db);
extern bool db_add_audit_event(sqlite3 *db, const char *username, const char *event_type, const char *details);
extern int db_ptt_start(sqlite3 *db, const char *username, const char *vfo, double frequency, const char *mode,
                        int bandwidth, float power, const char *record_file, const char *recording_id);
extern bool db_ptt_stop(sqlite3 *db, int session_id, int *duration_secs, const char *stop_reason);
extern int db_quota_get(sqlite3 *db, const char *username);
extern bool db_quota_spend(sqlite3 *db, const char *username, int secs);
extern bool db_quota_add(sqlite3 *db, const char *username, int credits);
extern bool db_quota_set(sqlite3 *db, const char *username, int credits);
extern bool db_quota_list(sqlite3 *db, int (*cb) (const char *name, int credits, void *user), void *user);
extern bool db_send_notice(rrconn_t *cptr, const char *msg_type, const char *text);
extern bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src, const char *msg_dest, const char *msg_type,
                            const char *msg_data);
extern bool db_send_chat_replay(rrconn_t *cptr, const char *channel);
extern bool db_room_ensure(sqlite3 *db, const char *name, bool has_vfos, uint32_t vfo_mask, const char *username);
extern bool db_room_set_topic(sqlite3 *db, const char *name, const char *topic);
extern char *db_room_get_topic(sqlite3 *db, const char *name);
extern bool db_room_status(sqlite3 *db, const char *name, bool *exists, bool *deleted);
extern bool db_room_restore(sqlite3 *db, const char *name, const char *username);
extern bool db_room_delete(sqlite3 *db, const char *name, const char *username, bool force, bool history);
extern char *db_room_list(sqlite3 *db);
extern bool db_room_vfos_clear(sqlite3 *db, const char *room);
extern bool db_room_vfo_add(sqlite3 *db, const char *room, const char *binding);
extern bool db_room_vfo_remove(sqlite3 *db, const char *room, const char *binding);
extern char *db_room_vfo_list(sqlite3 *db, const char *room);
extern char *db_room_vfo_map_list(sqlite3 *db);
/* Caller owns the returned UUID. Identity is stable for namespace + alias. */
extern char *db_rig_uuid_get_or_create(sqlite3 *db, const char *identity_namespace, const char *alias);
/* Caller owns the UUID. Persistent identity is rig UUID + config/native ID. */
extern char *db_vfo_uuid_get_or_create(sqlite3 *db, const char *rig_uuid, const char *config_id);
extern sqlite3 *masterdb;       // database.c
extern const char *replay_msg_type(const char *msg_type);
extern bool db_send_notice(rrconn_t *cptr, const char *msg_type, const char *text);

#endif // defined(USE_SQLITE)

#endif // !defined(__rrserver_database_h)

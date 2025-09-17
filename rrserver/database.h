//
// database.c: sqlite3 database stuff
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_database_h)
#define	__rr_database_h
#if	defined(FEATURE_SQLITE)
#include <sqlite3.h>

extern sqlite3 *db_open(const char *path);
extern bool db_add_user(sqlite3 *db, int uid, const char *name, bool enabled, const char *password, const char *email, int maxclones, const char *permissions);
extern bool db_add_audit_event(sqlite3 *db, const char *username, const char *event_type, const char *details);
extern int db_ptt_start(sqlite3 *db, const char *username, double frequency, const char *mode, int bandwidth, float power, const char *record_file);
extern bool db_ptt_stop(sqlite3 *db, int session_id);
extern bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src, const char *msg_dest, const char *msg_type, const char *msg_data);



//
extern sqlite3 *masterdb;

#endif	// defined(FEATURE_SQLITE)

#endif	// !defined(__rr_database_h)
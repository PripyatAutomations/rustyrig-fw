#ifndef RRSERVER_USAGE_H
#define RRSERVER_USAGE_H
#include <librustyaxe/core.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
struct rr_usage {
   struct rr_traffic traffic;
   uint64_t session_seconds, tx_seconds;
   int64_t bandwidth_remaining;
   bool bandwidth_limited;
   time_t reset_at;
};
bool db_usage_init(sqlite3 *db);
bool db_usage_get(sqlite3 *db, const char *name, struct rr_usage *usage);
bool db_usage_record(sqlite3 *db, const char *name, const struct rr_traffic *delta, uint64_t seconds);
bool db_usage_add_tx(sqlite3 *db, const char *name, uint64_t seconds);
bool db_usage_bandwidth_set(sqlite3 *db, const char *name, uint64_t bytes, bool add);
bool db_usage_reset(sqlite3 *db, const char *name, uint64_t allowance);
bool db_usage_reset_tx(sqlite3 *db, const char *name);
#endif
bool rr_usage_parse_bytes(const char *text, uint64_t *bytes);
void rrserver_usage_register_events(void);
bool rrserver_usage_flush_user(const char *name);
void rrserver_usage_quota(rrconn_t *requester, const char *command, int argc, char **argv);
#endif

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Cached directory metadata. Browsing never contacts receiver endpoints.
#ifndef RRCLIENT_RECEIVER_DIRECTORY_H
#define RRCLIENT_RECEIVER_DIRECTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <librustyaxe/dict.h>
typedef struct {
   const char *driver;          // First provider: kiwi. Other receiver types use their own policy.
   char *url, *name, *description, *location, *country, *contact, *antenna, *hardware, *grid, *properties;
   int users, users_max, ext_api;
   bool offline, flagged, deleted, favorite;
   long long last_seen, learned_at;
   char *local_state, *local_reason;
   long long state_updated, last_connected;
   uint64_t low_hz, high_hz;
} receiver_directory_entry_t;
/* Copied cross-thread catalog row; no borrowed pointers cross to GTK. */
typedef struct {
   char driver[32], url[512], name[512], description[512], location[512],
      country[128], contact[512], antenna[512], hardware[512], grid[64], properties[512], local_state[128], local_reason[512];
   int users, users_max, ext_api;
   bool offline, flagged, deleted, favorite;
   long long last_seen, learned_at, state_updated, last_connected;
   uint64_t low_hz, high_hz;
} rr_receiver_directory_snapshot_row_t;
typedef struct {
   uint32_t count;
   char status[512], info[1024], error[512], source[1024];
   uint8_t busy;
} rr_receiver_directory_snapshot_header_t;
size_t receiver_directory_count(void);
const receiver_directory_entry_t *receiver_directory_receiver(size_t index);
const receiver_directory_entry_t *receiver_directory_find_url(const char *url);
const char *receiver_directory_status(void);
char *receiver_directory_info(void);   // Caller frees.
bool receiver_directory_busy(void);
const char *receiver_directory_fetch_url(void);
const char *receiver_directory_last_error(void);
const char *receiver_directory_source_url(void);
bool receiver_directory_refresh(void);
/* No network activity until an upstream mirror is due for revalidation. */
bool receiver_directory_refresh_due(void);
/* Explicit user refresh: skip the disk-cache deadline and conditional HTTP validators. */
bool receiver_directory_refresh_force(bool force);
bool receiver_directory_import(const char *path);
/* Validated server snapshot, applied atomically to the client SQLite cache. */
bool receiver_directory_import_snapshot(const char *data, size_t length, const char *origin);
void receiver_directory_clear_server_snapshot(void);
void receiver_directory_queue_learn(const char *url, dict *fields);
bool receiver_directory_learn(const char *url, dict *fields);
bool receiver_directory_record_connection(const char *url);
bool receiver_directory_set_favorite(const char *url, bool favorite);
bool receiver_directory_set_state(const char *url, const char *state, const char *reason);
bool receiver_directory_entry_accessible(const receiver_directory_entry_t *receiver);
char *receiver_directory_bridge_config(const receiver_directory_entry_t *receiver);
void receiver_directory_shutdown(void);
#endif

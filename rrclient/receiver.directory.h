//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
   bool offline, flagged, deleted;
   long long last_seen, learned_at;
   char *local_state, *local_reason;
   long long state_updated;
   uint64_t low_hz, high_hz;
} receiver_directory_entry_t;
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
bool receiver_directory_import(const char *path);
void receiver_directory_queue_learn(const char *url, dict *fields);
bool receiver_directory_learn(const char *url, dict *fields);
bool receiver_directory_set_state(const char *url, const char *state, const char *reason);
bool receiver_directory_entry_accessible(const receiver_directory_entry_t *receiver);
char *receiver_directory_bridge_config(const receiver_directory_entry_t *receiver);
void receiver_directory_shutdown(void);
#endif

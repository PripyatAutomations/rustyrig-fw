//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_DATABASE_H
#define RRCLIENT_DATABASE_H
#include <stdbool.h>
/* Opens and initializes the persistent client settings database. */
bool rrclient_database_init(void);
void rrclient_database_shutdown(void);
bool rrclient_database_load_profiles(void);
bool rrclient_database_config_save(const char *path);
char *rrclient_database_normalize_url(const char *url);
bool rrclient_database_server_set(const char *name, const char *property, const char *value);
bool rrclient_database_server_delete(const char *name);
bool rrclient_database_server_move(const char *name, const char *network);
bool rrclient_database_network_set(const char *name, const char *members);
bool rrclient_database_proxy_set(const char *name, const char *url);
bool rrclient_database_proxy_delete(const char *name);
void rrclient_database_proxy_foreach(void (*cb) (const char *, const char *, void *), void *data);
typedef struct {
   int x, y, width, height;
   int zoom;
   bool rolled, minimized, pinned;
} rrclient_window_state_t;
/* Session-count expiration: untouched records are pruned after five runs. */
/* Named RX graph route presets; callers retain ownership of their strings. */
bool rrclient_database_audio_profile_save(const char *name, const char *const *ids,
                                           const char *routes, size_t count);
void rrclient_database_audio_profile_list(void (*cb)(const char *, void *), void *user);
void rrclient_database_audio_profile_load(const char *name,
   void (*cb)(const char *, char, void *), void *user);
bool rrclient_database_window_load(const char *id, rrclient_window_state_t *out);
bool rrclient_database_window_save(const char *id, const rrclient_window_state_t *state);
bool rrclient_database_load_themes(void);
bool rrclient_database_theme_set(const char *name, const char *property, const char *value);
#endif

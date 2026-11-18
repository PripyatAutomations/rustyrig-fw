//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_CONFIG_LAYERS_H
#define RRCLIENT_CONFIG_LAYERS_H
#include <librustyaxe/core.h>
char *rrclient_user_config_path(void);
bool rrclient_config_load(void);
void rrclient_theme_apply(void);
void rrclient_config_mark_dirty(void);
bool rrclient_config_is_dirty(void);
void rrclient_config_clear_dirty(void);
bool rrclient_config_reload(const char *path);
bool rrclient_config_reload_mode(const char *path, bool include_db_overrides);
void rrclient_config_reload_register(void);
#define RRCLIENT_CONFIG_RELOAD_REQUEST "client.config.reload"
typedef struct {
   bool from_db;
} rrclient_config_reload_request_t;
void rrclient_config_set_ignore_db(bool ignore);
bool rrclient_config_ignore_db(void);
bool rrclient_config_save(void);
/* Core-domain handler for local-only log verbosity changes from GTK. */
void rrclient_log_level_register(void);
#define RRCLIENT_LOG_LEVEL_REQUEST "client.log.level.apply"
typedef struct {
   char spec[2048];
} rrclient_log_level_request_t;
#endif

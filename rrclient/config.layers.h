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
/* Core-domain handler for local-only log verbosity changes from GTK. */
void rrclient_log_level_register(void);
#define RRCLIENT_LOG_LEVEL_REQUEST "client.log.level.apply"
typedef struct { char spec[2048]; } rrclient_log_level_request_t;
#endif

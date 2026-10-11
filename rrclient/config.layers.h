#ifndef RRCLIENT_CONFIG_LAYERS_H
#define RRCLIENT_CONFIG_LAYERS_H
#include <librustyaxe/core.h>
char *rrclient_user_config_path(void);
bool rrclient_config_load(void);
bool rrclient_config_reload(const char *path);
#endif

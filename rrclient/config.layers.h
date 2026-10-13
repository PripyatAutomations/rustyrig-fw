//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_CONFIG_LAYERS_H
#define RRCLIENT_CONFIG_LAYERS_H
#include <librustyaxe/core.h>
char *rrclient_user_config_path(void);
bool rrclient_config_load(void);
bool rrclient_config_reload(const char *path);
#endif

// rrserver/cfg.bridge.h: named bridge configuration.
#ifndef RRSERVER_CFG_BRIDGE_H
#define RRSERVER_CFG_BRIDGE_H
#include <stdbool.h>
bool rrserver_bridge_config_init(void);
const char *rrserver_bridge_config_get(const char *name, const char *key);
#endif

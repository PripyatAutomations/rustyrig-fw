//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Offline descriptions of configured receiver bridges; never live availability.
#ifndef RRSERVER_BRIDGE_HINTS_H
#define RRSERVER_BRIDGE_HINTS_H
#include <librrprotocol/bridge.ipc.h>
/* -1 invalid config, 0 unknown, 1 cached, 2 configured. */
int rrserver_bridge_hints_load(const char *name, const char *driver, rrp_bridge_ipc_message_t *out);
bool rrserver_bridge_hints_save(const char *name, const char *driver, const rrp_bridge_ipc_message_t *caps);
char *rrserver_bridge_metadata_get(const char *name, const char *driver, const char *field);
bool rrserver_bridge_metadata_set(const char *name, const char *driver, const char *field, const char *value);
#endif

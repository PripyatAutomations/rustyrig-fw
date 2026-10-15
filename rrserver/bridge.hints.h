// Offline descriptions of configured receiver bridges; never live availability.
#ifndef RRSERVER_BRIDGE_HINTS_H
#define RRSERVER_BRIDGE_HINTS_H
#include <librrprotocol/bridge.ipc.h>
/* -1 invalid config, 0 unknown, 1 cached, 2 configured. */
int rrserver_bridge_hints_load(const char *name, const char *driver, rrp_bridge_ipc_message_t *out);
bool rrserver_bridge_hints_save(const char *name, const char *driver, const rrp_bridge_ipc_message_t *caps);
#endif

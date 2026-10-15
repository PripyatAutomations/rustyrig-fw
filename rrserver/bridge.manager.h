// Server-owned external SDR bridge subprocess lifecycle.
#ifndef RRSERVER_BRIDGE_MANAGER_H
#define RRSERVER_BRIDGE_MANAGER_H
#include <stdbool.h>
#include <rrserver/backend.h>
const rr_backend_type_t *rrserver_bridge_backend_type(void);
bool rrserver_bridges_prepare(void);
bool rrserver_bridges_start(const char *config_file);
void rrserver_bridges_poll(void);
void rrserver_bridges_stop(void);
#endif

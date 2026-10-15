// Server-owned external SDR bridge subprocess lifecycle.
#ifndef RRSERVER_BRIDGE_MANAGER_H
#define RRSERVER_BRIDGE_MANAGER_H
#include <stdbool.h>
bool rrserver_bridges_start(const char *config_file);
void rrserver_bridges_poll(void);
void rrserver_bridges_stop(void);
#endif

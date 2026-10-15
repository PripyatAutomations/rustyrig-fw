//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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

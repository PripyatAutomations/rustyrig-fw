//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-owned bridge permissions. Operator restrictions remain independent.
#ifndef RRSERVER_BRIDGE_ACCESS_H
#define RRSERVER_BRIDGE_ACCESS_H
#include <librustyaxe/core.h>
#include <rrserver/rig.properties.h>
bool rrserver_bridge_can_use(const rrconn_t *client);
bool rrserver_bridge_can_admin(const rrconn_t *client);
bool rrserver_bridge_can_select_kiwi(const rrconn_t *client);
bool rrserver_bridge_rig(const rr_server_rig_t *radio);
void rrserver_bridge_access_register_events(void);
#endif

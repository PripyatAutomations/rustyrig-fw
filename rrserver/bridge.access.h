//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// Server-owned bridge permissions. Operator restrictions remain independent.
#ifndef RRSERVER_BRIDGE_ACCESS_H
#define RRSERVER_BRIDGE_ACCESS_H
#include <librustyaxe/core.h>
#include <rrserver/rig.properties.h>
bool rrserver_bridge_can_use(const rrconn_t *client);
bool rrserver_bridge_can_admin(const rrconn_t *client);
bool rrserver_websdr_can_connect(const rrconn_t *client);
bool rrserver_websdr_can_admin(const rrconn_t *client);
bool rrserver_bridge_rig(const rr_server_rig_t *radio);
void rrserver_bridge_access_register_events(void);
#endif

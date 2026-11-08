// rrserver/objects.c: Server authority for UUID discovery, subscriptions, and property
// controls.
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_SERVER_OBJECTS_H
#define RR_SERVER_OBJECTS_H
#include <librrprotocol/rrprotocol.h>
#include <rrserver/platform.controls.h>
void rrserver_objects_remote(rrconn_t *client, dict *message);
void rrserver_objects_room_state(rrconn_t *client, const char *room);
void rrserver_objects_register_events(void);
void rrserver_objects_fini(void);
void rrserver_objects_poll(void);
void rrserver_objects_platform_changed(const rr_platform_control_state_t *state);
#endif

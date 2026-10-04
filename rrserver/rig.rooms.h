// Per-rig room provisioning. Returns false on success.
#ifndef RRSERVER_RIG_ROOMS_H
#define RRSERVER_RIG_ROOMS_H
#include <stdbool.h>
extern bool rrserver_rig_rooms_init(void);
extern bool rrserver_rig_room_configured(const char *room);
struct rr_server_rig;
struct rr_server_rig *rrserver_rig_for_room(const char *room);
#endif

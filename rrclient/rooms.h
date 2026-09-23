#ifndef RRCLIENT_ROOMS_H
#define RRCLIENT_ROOMS_H

#include <stdbool.h>

bool rrclient_room_join(const char *room);
bool rrclient_room_part(const char *room);
bool rrclient_room_is_joined(const char *room);
void rrclient_rooms_clear(void);
const char *rrclient_current_room(void);
bool rrclient_room_set_vfos(const char *room, const char *vfos);
const char *rrclient_room_vfos(const char *room);

#endif

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_ROOMS_H
#define RRCLIENT_ROOMS_H

#include <stdbool.h>

bool rrclient_room_join(const char *room);
bool rrclient_room_request_join(const char *room);
bool rrclient_room_part(const char *room);
bool rrclient_room_is_joined(const char *room);
void rrclient_rooms_clear(void);
const char *rrclient_current_room(void);
bool rrclient_room_set_vfos(const char *room, const char *vfos);
bool rrclient_room_set_vfo_mask(const char *room, unsigned long mask);
const char *rrclient_room_vfos(const char *room);
bool rrclient_room_set_topic(const char *room, const char *topic);
const char *rrclient_room_topic(const char *room);

#endif

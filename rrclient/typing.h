//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_TYPING_H
#define RRCLIENT_TYPING_H

#include <stdbool.h>
#include <librrprotocol/rrprotocol.h>

/* Ephemeral room presence, scoped to the receiving connection.
 * The raw nickname and server identity never change for presentation. */
typedef enum {
   RR_TYPING_IDLE = 0,
   RR_TYPING_STARTED,
   RR_TYPING_ACTIVE,
   RR_TYPING_PAUSED
} rr_typing_state_t;

rr_typing_state_t rrclient_typing_get(const rrconn_t *connection, const char *room, const char *name);
/* Return true when any previously visible lease expired. */
bool rrclient_typing_prune(void);
void rrclient_typing_event(const char *event, const char *json, rrconn_t *connection, void *user);

#endif

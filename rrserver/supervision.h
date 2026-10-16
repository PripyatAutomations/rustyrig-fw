//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_SERVER_SUPERVISION_H
#define RR_SERVER_SUPERVISION_H
#include <librustyaxe/core.h>
/* Snapshot at key-down; survives grant revocation/disconnect in the PTT row. */
struct rr_supervision_attribution {
   char ticket[33];
   char elmers[HTTP_MAX_SESSIONS * (HTTP_USER_LEN + 2)];
};
void rrserver_supervision_snapshot(rrconn_t *client, const char *room, struct rr_supervision_attribution *out);
void rrserver_supervision_register(void);
void rrserver_supervision_tick(void);
bool rrserver_supervision_allowed(rrconn_t *client);
#endif

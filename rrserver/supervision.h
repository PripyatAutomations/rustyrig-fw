#ifndef RR_SERVER_SUPERVISION_H
#define RR_SERVER_SUPERVISION_H
#include <librustyaxe/core.h>
void rrserver_supervision_register(void);
void rrserver_supervision_tick(void);
bool rrserver_supervision_allowed(rrconn_t *client);
#endif

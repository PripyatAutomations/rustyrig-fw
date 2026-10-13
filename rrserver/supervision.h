//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_SERVER_SUPERVISION_H
#define RR_SERVER_SUPERVISION_H
#include <librustyaxe/core.h>
void rrserver_supervision_register(void);
void rrserver_supervision_tick(void);
bool rrserver_supervision_allowed(rrconn_t *client);
#endif

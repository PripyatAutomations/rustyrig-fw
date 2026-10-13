//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_SUPERVISION_H
#define RRCLIENT_SUPERVISION_H
#include <librustyaxe/core.h>
void rrclient_supervision_result(const char *, const char *, rrconn_t *, void *);
void rrclient_supervision_clear(rrconn_t *);
bool rrclient_supervision_granted(rrconn_t *, const char *);
#endif

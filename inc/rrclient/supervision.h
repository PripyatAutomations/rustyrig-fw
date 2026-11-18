//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_SUPERVISION_H
#define RRCLIENT_SUPERVISION_H
#include <librustyaxe/core.h>
void rrclient_supervision_result(const char *, const char *, rrconn_t *, void *);
void rrclient_supervision_clear(rrconn_t *);
bool rrclient_supervision_granted(rrconn_t *, const char *);
#endif

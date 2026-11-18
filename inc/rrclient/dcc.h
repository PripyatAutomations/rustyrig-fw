//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_DCC_H
#define RRCLIENT_DCC_H
#include <librustyaxe/core.h>
bool cmd_dcc(int argc, char **args);
void rrclient_dcc_register_events(void);
void rrclient_dcc_poll(void);
void rrclient_dcc_capture(const void *pcm, size_t length);
#endif

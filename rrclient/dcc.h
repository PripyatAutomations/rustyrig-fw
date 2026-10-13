//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_DCC_H
#define RRCLIENT_DCC_H
#include <librustyaxe/core.h>
bool cmd_dcc(int argc, char **args);
void rrclient_dcc_register_events(void);
void rrclient_dcc_poll(void);
void rrclient_dcc_capture(const void *pcm, size_t length);
#endif

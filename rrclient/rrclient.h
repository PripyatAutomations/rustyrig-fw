//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrclient/rrclient.h
#ifndef __rrclient_rrclient_h
#define __rrclient_rrclient_h

#include <stdbool.h>

bool rrclient_connect(const char *url);
bool rrclient_disconnect(void);
void rrclient_poll_events(void);
bool rrclient_autoconnect(void);

#endif // __rrclient_rrclient_h

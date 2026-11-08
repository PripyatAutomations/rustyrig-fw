//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
// rrclient/rrclient.h
#ifndef __rrclient_rrclient_h
#define __rrclient_rrclient_h

#include <stdbool.h>
#include <glib.h>

/* Core scheduler context: dedicated for GTK, default for TUI. */
GMainContext *rrclient_core_context(void);
bool rrclient_connect(const char *url);
bool rrclient_disconnect(void);
void rrclient_poll_events(void);
bool rrclient_autoconnect(void);

#endif // __rrclient_rrclient_h

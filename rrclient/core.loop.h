//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_CORE_LOOP_H
#define RRCLIENT_CORE_LOOP_H
#include <glib.h>
#include <stdbool.h>

/* Dedicated core GLib context. GTK stays on the process main thread. Work items and their payloads must be independent of GTK and caller stacks.
 */
bool rrclient_core_loop_start(void);
bool rrclient_core_loop_post(GSourceFunc callback, gpointer payload, GDestroyNotify destroy);
void rrclient_core_loop_stop(void);
bool rrclient_core_loop_is_owner(void);
#endif

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
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

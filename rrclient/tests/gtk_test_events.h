// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* Single-owner UI fixtures use real named-domain subscriptions. Cross-thread queueing and ownership are exercised separately by thread_dispatch.c. */
#ifndef RR_GTK_TEST_EVENTS_H
#define RR_GTK_TEST_EVENTS_H
#include <assert.h>
#include <librustyaxe/event-bus.h>
static void gtk_test_dispatch(event_dispatch_fn_t callback, void *data, void *user) {
   (void)user;
   callback(data);
}
static void gtk_test_events_init(void) {
   event_init();
   assert(event_domain_register("gtk", gtk_test_dispatch, NULL));
   assert(event_domain_register("core", gtk_test_dispatch, NULL));
}
#endif

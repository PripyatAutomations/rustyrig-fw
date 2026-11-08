//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
/* Connection progress events; no GTK dependency in the core client. */
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <rrclient/connecting.h>

void rrclient_connect_progress(const char *profile, unsigned phase) {
   if (!profile || !*profile) {
      return;
   }
   rr_connect_progress_t value = {
      0
   };
   snprintf(value.profile, sizeof(value.profile), "%s", profile);
   value.phase = phase;
   event_emit_message(&(rr_event_message_t) {
      .name = RR_CONNECT_PROGRESS_EVENT, .sender = "core", .payload = &value, .length = sizeof(value)
   });
}

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
/* Only use for IDs returned by g_source_attach(..., rrclient_core_context()).
 * g_source_remove() searches the process-default context, which is the GTK context in threaded clients and cannot see these core-owned sources. Clear stale
 * IDs, and never destroy an unrelated source in another context. */
static inline void rrclient_core_source_cancel(guint *id) {
   if (!id || !*id) {
      return;
   }
   GSource *source = g_main_context_find_source_by_id(rrclient_core_context(), *id);

   if (source) {
      g_source_destroy(source);
   }
   *id = 0;
}

bool rrclient_connect(const char *url);
bool rrclient_disconnect(void);
void rrclient_poll_events(void);
bool rrclient_autoconnect(void);

#endif // __rrclient_rrclient_h

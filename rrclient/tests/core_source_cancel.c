// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// Run: cc -I. rrclient/tests/core_source_cancel.c $(pkg-config --cflags --libs glib-2.0) -o /tmp/core-source-test && /tmp/core-source-test
#include <assert.h>
#include <stdio.h>
#include <rrclient/rrclient.h>

static GMainContext *core;
GMainContext *rrclient_core_context(void) {
   return core;
}

static gboolean once(gpointer data) {
   guint *calls = data;
   (*calls)++;

   return G_SOURCE_REMOVE;
}

int main(void) {
   core = g_main_context_new();
   guint calls = 0;
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, once, &calls, NULL);
   guint id = g_source_attach(source, core);
   g_source_unref(source);
   assert(id != 0);
   assert(g_main_context_find_source_by_id(core, id));

   /* A timer attached outside the default context must be removed from its actual owner, without a spurious GLib critical or invoking its callback. */
   rrclient_core_source_cancel(&id);
   assert(id == 0);
   assert(!g_main_context_pending(core));
   assert(calls == 0);

   /* Callback-completed source IDs may remain in asynchronous state during shutdown. Cancellation must tolerate their absence. */
   source = g_idle_source_new();
   g_source_set_callback(source, once, &calls, NULL);
   id = g_source_attach(source, core);
   g_source_unref(source);
   assert(g_main_context_iteration(core, FALSE));
   assert(calls == 1);
   rrclient_core_source_cancel(&id);
   assert(id == 0);
   rrclient_core_source_cancel(&id);

   g_main_context_unref(core);
   puts("PASS: owner-context timers cancel without stale source IDs");

   return 0;
}

// Exercise the real GTK scheduler alongside independent UI work.
#include <assert.h>
#define main rrclient_application_main
#include "rrclient/main.c"
#undef main

static unsigned network_polls, reconnect_polls, ui_ticks;
static gint64 max_ui_gap, last_ui_tick;
void mg_mgr_poll(struct mg_mgr *manager, int wait_ms) {
   assert(wait_ms == 0);
}
void rrclient_direct_poll(void) {
}
void rrclient_dcc_poll(void) {
}
void rrclient_poll_events(void) {
   network_polls++;
   mg_mgr_poll(&mgr, 0);
   // Independent UI work must also be ready while the network is serviced.
}
void rrclient_poll_events_reconnect(void) {
   reconnect_polls++;
}
static gboolean ui_tick(gpointer unused) {
   gint64 current = g_get_monotonic_time();

   if (last_ui_tick && current - last_ui_tick > max_ui_gap) {
      max_ui_gap = current - last_ui_tick;
   }
   last_ui_tick = current;
   ui_ticks++;

   return G_SOURCE_CONTINUE;
}
int main(void) {
   poll_mongoose_init();
   guint ui_source = g_timeout_add(1, ui_tick, NULL);
   gint64 start = g_get_monotonic_time();
   // Measure dispatch cost directly too: the transport callback must not wait.
   ws_poll_cb(NULL);
   assert(g_get_monotonic_time() - start < 4000);
   while (g_get_monotonic_time() - start < 500000) {
      g_main_context_iteration(NULL, TRUE);
   }
   assert(network_polls >= 30 && network_polls <= 65);
   assert(reconnect_polls == network_polls);
   assert(ui_ticks > network_polls * 3);
   assert(max_ui_gap < 30000);
   g_source_remove(ui_source);
   unsigned before_shutdown = network_polls;
   dying = true;
   assert(ws_poll_cb(NULL) == G_SOURCE_REMOVE && network_polls == before_shutdown);
   puts("PASS: GTK transport preserves 10ms cadence without blocking independent UI work");

   return 0;
}

#include <assert.h>
#define main rrclient_application_main
#define g_get_monotonic_time poll_test_monotonic_time
#include "rrclient/main.c"
#undef g_get_monotonic_time
#undef main

static gint64 test_clock;
gint64 poll_test_monotonic_time(void) {
   return test_clock;
}

int main(void) {
   gint timeout;
   mg_next_poll_us = 10000;
   test_clock = 9001;
   assert(!mg_source_prepare(NULL, &timeout) && timeout == 1);
   test_clock = 9999;
   assert(!mg_source_prepare(NULL, &timeout) && timeout == 1);
   test_clock = 8999;
   assert(!mg_source_prepare(NULL, &timeout) && timeout == 2);
   test_clock = 10000;
   assert(mg_source_prepare(NULL, &timeout) && timeout == 0);
   assert(mg_next_poll_us == test_clock + MG_POLL_INTERVAL_US);
   assert(!mg_source_check(NULL));
   test_clock = 50000;
   assert(mg_source_prepare(NULL, &timeout) && timeout == 0);
   assert(mg_next_poll_us == 60000);
   puts("PASS: GTK network source sleeps through fractional milliseconds and preserves poll cadence");
   return 0;
}

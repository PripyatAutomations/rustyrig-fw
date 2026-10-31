#include <glib.h>
#include <assert.h>
#include "../thread.dispatch.h"

static int delivered, released;
static gboolean receive(gpointer data) {
   int *value = data;
   assert(*value == 42);
   delivered++;
   return G_SOURCE_REMOVE;
}
static void release(gpointer data) {
   released++;
   g_free(data);
}
int main(void) {
   GMainContext *ctx = g_main_context_new();
   rr_thread_dispatch_t *bridge = rr_thread_dispatch_new(ctx);
   int *value = g_new(int, 1);
   *value = 42;
   assert(rr_thread_dispatch_post(bridge, receive, value, release));
   while (delivered == 0) g_main_context_iteration(ctx, TRUE);
   assert(released == 1);
   rr_thread_dispatch_close(bridge);
   value = g_new(int, 1);
   *value = 42;
   assert(!rr_thread_dispatch_post(bridge, receive, value, release));
   assert(released == 2);
   rr_thread_dispatch_free(bridge);
   g_main_context_unref(ctx);
   return 0;
}

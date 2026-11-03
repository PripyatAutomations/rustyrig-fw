#include <glib.h>
#include <assert.h>
#include "../thread.dispatch.h"

static gint delivered, released;
static GThread *owner;
static gboolean receive(gpointer data) {
   int *value = data;
   assert(g_thread_self() == owner);
   assert(*value == 42);
   g_atomic_int_inc(&delivered);

   return G_SOURCE_REMOVE;
}
static void release(gpointer data) {
   g_atomic_int_inc(&released);
   g_free(data);
}
static gpointer producer(gpointer data) {
   rr_thread_dispatch_t *bridge = data;
   int *value = g_new(int, 1);
   *value = 42;
   assert(rr_thread_dispatch_post(bridge, receive, value, release));

   return NULL;
}
int main(void) {
   owner = g_thread_self();
   GMainContext *ctx = g_main_context_new();
   rr_thread_dispatch_t *bridge = rr_thread_dispatch_new(ctx);
   GThread *worker = g_thread_new("dispatch-producer", producer, bridge);
   while (!g_atomic_int_get(&delivered)) {
      g_main_context_iteration(ctx, TRUE);
   }
   g_thread_join(worker);
   assert(g_atomic_int_get(&released) == 1);

   rr_thread_dispatch_close(bridge);
   int *value = g_new(int, 1);
   *value = 42;
   assert(!rr_thread_dispatch_post(bridge, receive, value, release));
   assert(g_atomic_int_get(&released) == 2);
   rr_thread_dispatch_free(bridge);
   g_main_context_unref(ctx);

   return 0;
}

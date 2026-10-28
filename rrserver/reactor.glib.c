#include "reactor.glib.h"
#include <glib-unix.h>
#include <stdlib.h>

struct rr_reactor_watch {
   GSource *source;
   rr_reactor_ready_fn callback;
   void *context;
};

static gboolean rr_reactor_fd_cb(gint fd, GIOCondition events, gpointer arg) {
   rr_reactor_watch_t *watch = arg;
   return watch->callback(fd, events, watch->context) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

rr_reactor_watch_t *rr_reactor_watch_fd(GMainContext *loop, int fd,
   GIOCondition interests, rr_reactor_ready_fn callback, void *context) {
   if (fd < 0 || !callback || !(interests & (G_IO_IN | G_IO_OUT))) return NULL;
   rr_reactor_watch_t *watch = calloc(1, sizeof(*watch));
   if (!watch) return NULL;
   watch->callback = callback;
   watch->context = context;
   watch->source = g_unix_fd_source_new(fd, interests | G_IO_ERR | G_IO_HUP | G_IO_NVAL);
   if (!watch->source) { free(watch); return NULL; }
   g_source_set_callback(watch->source, (GSourceFunc)(void (*)(void))rr_reactor_fd_cb, watch, free);
   if (!g_source_attach(watch->source, loop)) {
      g_source_destroy(watch->source);
      g_source_unref(watch->source);
      return NULL;
   }
   g_source_unref(watch->source);
   return watch;
}

/* Calling cancel during a live callback is supported. Once the callback
 * returns false or cancel is called, the watch pointer must not be reused. */
void rr_reactor_watch_cancel(rr_reactor_watch_t *watch) {
   if (watch) g_source_destroy(watch->source);
}

void rr_reactor_dispatch_ready(GMainContext *loop) {
   GMainContext *ctx = loop ? loop : g_main_context_default();
   /* Bound dispatch so a busy mesh socket cannot starve the client loop. */
   for (unsigned i = 0; i < 64 && g_main_context_pending(ctx); ++i)
      g_main_context_iteration(ctx, FALSE);
}

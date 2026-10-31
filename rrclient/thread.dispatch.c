#include "thread.dispatch.h"

struct rr_thread_dispatch {
   GMainContext *context;
   GMutex lock;
   bool closed;
};

typedef struct {
   GSourceFunc fn;
   gpointer data;
   GDestroyNotify destroy;
} rr_dispatch_command_t;

static gboolean dispatch_run(gpointer data) {
   rr_dispatch_command_t *command = data;
   command->fn(command->data);
   return G_SOURCE_REMOVE;
}

static void dispatch_destroy(gpointer data) {
   rr_dispatch_command_t *command = data;
   if (command->destroy) command->destroy(command->data);
   g_free(command);
}

rr_thread_dispatch_t *rr_thread_dispatch_new(GMainContext *context) {
   if (!context) return NULL;
   rr_thread_dispatch_t *dispatch = g_new0(rr_thread_dispatch_t, 1);
   g_mutex_init(&dispatch->lock);
   dispatch->context = g_main_context_ref(context);
   return dispatch;
}

bool rr_thread_dispatch_post(rr_thread_dispatch_t *dispatch,
                             GSourceFunc fn, gpointer data,
                             GDestroyNotify destroy) {
   if (!dispatch || !fn) {
      if (destroy) destroy(data);
      return false;
   }
   rr_dispatch_command_t *command = g_new(rr_dispatch_command_t, 1);
   *command = (rr_dispatch_command_t){fn, data, destroy};
   g_mutex_lock(&dispatch->lock);
   if (dispatch->closed) {
      g_mutex_unlock(&dispatch->lock);
      dispatch_destroy(command);
      return false;
   }
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, dispatch_run, command, dispatch_destroy);
   guint id = g_source_attach(source, dispatch->context);
   g_source_unref(source);
   g_mutex_unlock(&dispatch->lock);
   if (!id) {
      /* GLib finalizes the source (and its payload) when unreffed. */
      return false;
   }
   return true;
}

void rr_thread_dispatch_close(rr_thread_dispatch_t *dispatch) {
   if (!dispatch) return;
   g_mutex_lock(&dispatch->lock);
   dispatch->closed = true;
   g_mutex_unlock(&dispatch->lock);
}

void rr_thread_dispatch_free(rr_thread_dispatch_t *dispatch) {
   if (!dispatch) return;
   rr_thread_dispatch_close(dispatch);
   g_main_context_unref(dispatch->context);
   g_mutex_clear(&dispatch->lock);
   g_free(dispatch);
}

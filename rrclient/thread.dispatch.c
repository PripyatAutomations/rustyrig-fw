#include "thread.dispatch.h"

/* The source's destroy notify releases a queued reference, making the
 * mailbox safe to close while posted callbacks are still pending. */
struct rr_thread_mailbox {
   GMainContext *context;
   GMutex lock;
   gboolean open;
   guint refs;
};
typedef struct {
   rr_thread_mailbox_t *box;
   GSourceFunc callback;
   gpointer data;
   GDestroyNotify destroy;
} dispatch_t;

static void mailbox_unref(rr_thread_mailbox_t *box) {
   gboolean last;
   g_mutex_lock(&box->lock);
   last = (--box->refs == 0);
   g_mutex_unlock(&box->lock);
   if (!last) return;
   g_main_context_unref(box->context);
   g_mutex_clear(&box->lock);
   g_free(box);
}
rr_thread_mailbox_t *rr_thread_mailbox_new(GMainContext *context) {
   if (!context) return NULL;
   rr_thread_mailbox_t *box = g_new0(rr_thread_mailbox_t, 1);
   box->context = g_main_context_ref(context);
   g_mutex_init(&box->lock);
   box->refs = 1;
   box->open = TRUE;
   return box;
}
static gboolean dispatch_run(gpointer opaque) {
   dispatch_t *job = opaque;
   g_mutex_lock(&job->box->lock);
   gboolean open = job->box->open;
   g_mutex_unlock(&job->box->lock);
   if (open) job->callback(job->data);
   return G_SOURCE_REMOVE;
}
static void dispatch_free(gpointer opaque) {
   dispatch_t *job = opaque;
   if (job->destroy) job->destroy(job->data);
   mailbox_unref(job->box);
   g_free(job);
}
gboolean rr_thread_mailbox_post(rr_thread_mailbox_t *box,
                                GSourceFunc callback, gpointer data,
                                GDestroyNotify destroy) {
   if (!box || !callback) {
      if (destroy) destroy(data);
      return FALSE;
   }
   g_mutex_lock(&box->lock);
   if (!box->open) {
      g_mutex_unlock(&box->lock);
      if (destroy) destroy(data);
      return FALSE;
   }
   ++box->refs;
   dispatch_t *job = g_new(dispatch_t, 1);
   *job = (dispatch_t){box, callback, data, destroy};
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, dispatch_run, job, dispatch_free);
   guint attached = g_source_attach(source, box->context);
   g_source_unref(source);
   g_mutex_unlock(&box->lock);
   return attached != 0;
}
void rr_thread_mailbox_close(rr_thread_mailbox_t *box) {
   if (!box) return;
   g_mutex_lock(&box->lock);
   box->open = FALSE;
   g_mutex_unlock(&box->lock);
}
void rr_thread_mailbox_free(rr_thread_mailbox_t *box) {
   if (!box) return;
   rr_thread_mailbox_close(box);
   mailbox_unref(box);
}

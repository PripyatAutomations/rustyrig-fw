#ifndef RRCLIENT_THREAD_DISPATCH_H
#define RRCLIENT_THREAD_DISPATCH_H
#include <glib.h>

/* A mailbox bound to a GLib context; payload ownership transfers on post.
 * A post that cannot be delivered destroys its payload immediately.
 * Callbacks always run on the destination context (never the posting thread).
 */
typedef struct rr_thread_mailbox rr_thread_mailbox_t;
rr_thread_mailbox_t *rr_thread_mailbox_new(GMainContext *context);
gboolean rr_thread_mailbox_post(rr_thread_mailbox_t *box,
                                GSourceFunc callback, gpointer data,
                                GDestroyNotify destroy);
void rr_thread_mailbox_close(rr_thread_mailbox_t *box);
void rr_thread_mailbox_free(rr_thread_mailbox_t *box);
#endif

//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_THREAD_DISPATCH_H
#define RRCLIENT_THREAD_DISPATCH_H

#include <glib.h>
#include <stdbool.h>

/* A dispatcher targets one explicitly owned GLib context. Calling post transfers ownership of data to destroy, including on dispatch failure. Callbacks must
 * return G_SOURCE_REMOVE (one-shot). */
typedef struct rr_thread_dispatch rr_thread_dispatch_t;

rr_thread_dispatch_t *rr_thread_dispatch_new(GMainContext *context);
bool rr_thread_dispatch_post(rr_thread_dispatch_t *dispatch, GSourceFunc fn, gpointer data, GDestroyNotify destroy);
void rr_thread_dispatch_close(rr_thread_dispatch_t *dispatch);
void rr_thread_dispatch_free(rr_thread_dispatch_t *dispatch);
#endif

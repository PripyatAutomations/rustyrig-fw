#ifndef RR_SERVER_REACTOR_GLIB_H
#define RR_SERVER_REACTOR_GLIB_H
#include <glib.h>
#include <stdbool.h>
/* Platform adapter for fd readiness; rr_transport itself stays portable.
 * The caller owns the file descriptor. Callback false removes the watch. */
typedef struct rr_reactor_watch rr_reactor_watch_t;
typedef bool (*rr_reactor_ready_fn)(int fd, GIOCondition events, void *context);
rr_reactor_watch_t *rr_reactor_watch_fd(GMainContext *loop, int fd,
   GIOCondition interests, rr_reactor_ready_fn callback, void *context);
void rr_reactor_watch_cancel(rr_reactor_watch_t *watch);
void rr_reactor_dispatch_ready(GMainContext *loop);
#endif

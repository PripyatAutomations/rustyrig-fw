// librustyaxe/event-bus.h:
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librustyaxe_event_bus_h)
#define __librustyaxe_event_bus_h

#include <stddef.h>

/*
 * Normal event callbacks.
 *
 * Synchronous callbacks are called directly from event_emit() in the
 * emitter's thread. event/data are owned by the emitter and are valid
 * only for the duration of the callback.
 */
typedef void (*event_cb_t)(
   const char *event,
   const char *data,
   rrconn_t *cptr,
   void *user
);

/*
 * Binary payload event callback.
 *
 * For synchronous listeners, data is owned by the emitter and is only
 * valid for the duration of the callback.
 *
 * Dispatched listeners receive an internally-owned copy which remains
 * valid for the duration of the callback.
 */
typedef void (*event_binary_cb_t)(
   const char *event,
   const void *data,
   size_t len,
   rrconn_t *cptr,
   void *user
);

/*
 * Generic asynchronous dispatcher.
 *
 * The event bus calls:
 *
 *    dispatch(fn, arg, dispatch_user);
 *
 * The dispatcher must arrange for fn(arg) to eventually be called.
 * Once dispatch() has accepted the request, ownership of arg belongs
 * to fn().
 *
 * This deliberately has no GLib dependency. A GTK frontend can wrap
 * g_main_context_invoke() here.
 */
typedef void (*event_dispatch_fn_t)(void *arg);

typedef void (*event_dispatch_t)(
   event_dispatch_fn_t fn,
   void *arg,
   void *user
);

/*
 * Listener structures.
 *
 * dispatch == NULL means that the listener is synchronous and is called
 * directly from the thread which emitted the event.
 */
typedef struct event_listener {
   event_cb_t cb;
   void *user;

   event_dispatch_t dispatch;
   void *dispatch_user;
} event_listener_t;

typedef struct event_binary_listener {
   event_binary_cb_t cb;
   void *user;

   event_dispatch_t dispatch;
   void *dispatch_user;
} event_binary_listener_t;


/*
 * Event bus lifetime.
 */
extern void event_init(void);
extern void event_shutdown(void);


/*
 * Register synchronous listeners.
 *
 * These callbacks execute immediately in the thread calling event_emit()
 * or event_emit_binary().
 */
extern void event_on(
   const char *event,
   event_cb_t cb,
   void *user
);

extern void event_on_binary(
   const char *event,
   event_binary_cb_t cb,
   void *user
);


/*
 * Compatibility alias for event_on_binary().
 */
extern void event_register_binary(
   const char *event,
   event_binary_cb_t cb,
   void *user
);


/*
 * Register dispatched listeners.
 *
 * Instead of executing the callback directly, the event bus makes an
 * owned copy of the event/payload and hands execution to dispatch().
 *
 * This allows callers to marshal callbacks onto another event loop or
 * thread without making librustyaxe depend on that event-loop library.
 *
 * rrconn_t *cptr is NOT copied or reference counted. If a dispatched
 * callback needs to dereference cptr, its lifetime must be guaranteed
 * separately.
 */
extern void event_on_dispatch(
   const char *event,
   event_cb_t cb,
   void *user,
   event_dispatch_t dispatch,
   void *dispatch_user
);

extern void event_on_binary_dispatch(
   const char *event,
   event_binary_cb_t cb,
   void *user,
   event_dispatch_t dispatch,
   void *dispatch_user
);


/*
 * Emit events.
 */
extern void event_emit(
   const char *event,
   rrconn_t *cptr,
   const char *data
);

extern void event_emit_binary(
   const char *event,
   rrconn_t *cptr,
   const void *data,
   size_t len
);

extern void event_emit_dict(
   const char *event,
   rrconn_t *cptr,
   dict *data
);


/*
 * Remove listeners.
 *
 * cb == NULL and/or user == NULL act as wildcards, preserving the
 * behavior of the original event_off().
 */
extern void event_off(
   const char *event,
   event_cb_t cb,
   void *user
);

extern void event_off_binary(
   const char *event,
   event_binary_cb_t cb,
   void *user
);

#endif // !defined(__librustyaxe_event_bus_h)
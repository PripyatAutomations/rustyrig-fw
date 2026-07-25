//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__librustyaxe_event_bus_h)
#define __librustyaxe_event_bus_h

typedef void (*event_cb_t)(const char *event, void *data, irc_conn_t *cptr, void *user);         

typedef struct event_listener {
   event_cb_t cb;
   void *user;
} event_listener_t;

extern void event_init(void);
extern void event_on(const char *event, event_cb_t cb, void *user);
extern void event_emit(const char *event, irc_conn_t *cptr, void *data);
extern void event_off(const char *event, event_cb_t cb, void *user);
extern void event_shutdown(void);

#endif	// #defined __librustyaxe_event_bus_h

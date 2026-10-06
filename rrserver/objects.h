// rrserver/objects.c: Server authority for UUID discovery, subscriptions, and property
// controls.
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_SERVER_OBJECTS_H
#define	RR_SERVER_OBJECTS_H
void rrserver_objects_register_events(void);
void rrserver_objects_fini(void);
void rrserver_objects_poll(void);
#endif

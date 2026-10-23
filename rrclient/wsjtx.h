//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_WSJTX_H
#define RRCLIENT_WSJTX_H
#include <stddef.h>
/* Snapshot of the local WSJT-X UDP listener and observed peer activity. Called on the GLib main thread, just like the listener callback. */
void rrclient_wsjtx_status(char *state, size_t state_len, char *connection, size_t connection_len, char *details, size_t details_len);
typedef void (*rr_wsjtx_field_cb)(const char *instance, const char *group, const char *key, const char *value, void *data);
void rrclient_wsjtx_foreach_field(rr_wsjtx_field_cb callback, void *data);
#endif

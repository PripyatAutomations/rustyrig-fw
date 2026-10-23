#ifndef RRCLIENT_WSJTX_H
#define RRCLIENT_WSJTX_H
#include <stddef.h>
/* Snapshot of the local WSJT-X UDP listener and observed peer activity.
 * Called on the GLib main thread, just like the listener callback. */
void rrclient_wsjtx_status(char *state, size_t state_len,
                           char *connection, size_t connection_len,
                           char *details, size_t details_len);
#endif

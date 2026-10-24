//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_MEDIA_DIRECT_H
#define RRCLIENT_MEDIA_DIRECT_H
#include <librustyaxe/core.h>
typedef enum {
   RR_MEDIA_AUTO, RR_MEDIA_DIRECT, RR_MEDIA_PROXY
} rr_media_transport_t;
bool rrclient_direct_preferred(const char *profile);
bool rrclient_direct_set_preference(rrconn_t *client, bool enabled);
void rrclient_direct_apply_preferences(void);
bool rrclient_direct_set_mode(rrconn_t *client, const char *uuid, rr_media_transport_t mode);
void rrclient_direct_register_events(void);
void rrclient_direct_message(rrconn_t *client, dict *message);
void rrclient_direct_poll(void);
/* Accepted RTP media payload bytes for this connection, separately from WS/IRC. */
bool rrclient_direct_traffic(rrconn_t *client, uint64_t *rx, uint64_t *tx);
#endif

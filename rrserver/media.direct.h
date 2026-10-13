//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_MEDIA_DIRECT_H
#define RRSERVER_MEDIA_DIRECT_H
#include <librustyaxe/core.h>
bool rrserver_direct_relay_grant(rrconn_t *client, dict *reply, unsigned stream, unsigned direction);
bool rrserver_direct_config_init(void);
bool rrserver_rtp_ports(const char *text, unsigned *first, unsigned *last);
void rrserver_direct_register_events(void);
void rrserver_direct_close(rrconn_t *client);
bool rrserver_direct_listening(void);
bool rrserver_direct_tx_ready(rrconn_t *client, const char *room);
#endif

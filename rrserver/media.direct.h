#ifndef RRSERVER_MEDIA_DIRECT_H
#define RRSERVER_MEDIA_DIRECT_H
#include <librustyaxe/core.h>
bool rrserver_direct_relay_grant(rrconn_t *client, dict *reply, unsigned stream, unsigned direction);
bool rrserver_direct_config_init(void);
bool rrserver_rtp_ports(const char *text, unsigned *first, unsigned *last);
void rrserver_direct_register_events(void);
void rrserver_direct_close(rrconn_t * client);
bool rrserver_direct_listening(void);
bool rrserver_direct_tx_ready(rrconn_t * client, const char *room);
#endif

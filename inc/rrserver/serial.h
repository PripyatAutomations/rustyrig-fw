// rrserver/serial.h: Support for serial channels
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_SERIAL_H
#define RRSERVER_SERIAL_H
#include <stdbool.h>
#include <stddef.h>
struct rr_server_rig;
struct rrconn;
extern void rrserver_serial_init(void);
extern void rrserver_serial_fini(void);
extern bool rrserver_serial_poll(void);
extern void rrserver_serial_client_exports(const struct rrconn *client, char *out, size_t size);
extern bool rrserver_serial_rx_demand(const struct rr_server_rig *radio);
extern void rrserver_serial_rx_pcm(struct rr_server_rig *radio, const void *samples, size_t len);
#endif

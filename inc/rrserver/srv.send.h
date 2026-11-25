/* Private conversation replies; never broadcast. SPDX-License-Identifier: GPL-2.0-only */
#ifndef RR_SERVER_SEND_H
#define RR_SERVER_SEND_H
#include <librrprotocol/rrprotocol.h>
bool rrserver_send_target_error(rrconn_t *client, const char *target, const char *format, ...) __attribute__((format(printf,3,4)));
bool rrserver_send_target_notice(rrconn_t *client, const char *target, const char *format, ...) __attribute__((format(printf,3,4)));
#endif

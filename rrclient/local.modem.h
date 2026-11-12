// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_LOCAL_MODEM_H
#define RRCLIENT_LOCAL_MODEM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/event-bus.h>
#define RR_LOCAL_MODEM_MAX 8
#define RR_LOCAL_MODEM_STREAMS 48
#define RR_LOCAL_MODEM_TEXT 2048
#define RR_LOCAL_MODEM_REQUEST "client.modem.decoder.request"
#define RR_LOCAL_MODEM_RESULT "client.modem.decoder.snapshot"
typedef enum {
   RR_LOCAL_MODEM_SNAPSHOT = 1,
   RR_LOCAL_MODEM_START,
   RR_LOCAL_MODEM_STOP
} rr_local_modem_action_t;
typedef struct {
   rr_local_modem_action_t action;
   unsigned slot;             /* 1..8; 0 = first free slot on START */
   char source[64];
   char engine[16];           /* psk / olivia / ft8 */
   char option[24];           /* BPSK31 or Olivia tones */
   unsigned value;            /* carrier Hz or Olivia bandwidth */
} rr_local_modem_request_t;
typedef struct {
   unsigned slot, value;
   bool running;
   char source[64], engine[16], option[24];
   guint64 drops, received;
   char text[RR_LOCAL_MODEM_TEXT];
} rr_local_modem_entry_t;
typedef struct {
   unsigned count, sources;
   rr_local_modem_entry_t entries[RR_LOCAL_MODEM_MAX];
   char source_id[RR_LOCAL_MODEM_STREAMS][64];
   char status[192];
} rr_local_modem_snapshot_t;
void rr_local_modem_init(void);
void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames);
void rr_local_modem_shutdown(void);
void rr_local_modem_request(const rr_local_modem_request_t *request);
bool cmd_local_modem(int argc, char **argv);
#endif

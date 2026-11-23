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
#define RR_LOCAL_MODEM_DECODE "client.modem.decoder.decode"
#define RR_LOCAL_MODEM_STREAM_RX "client.modem.stream.rx"
typedef struct {
   unsigned slot;
   unsigned length;
   char bytes[256];
} rr_local_modem_stream_rx_t;
typedef struct {
   unsigned slot;
   char source[64], engine[16], mode[24];
   uint64_t frequency_hz;       /* RF dial frequency at decode time; 0 = unavailable. */
   char line[RR_LOCAL_MODEM_TEXT];
} rr_local_modem_decode_t;
typedef enum {
   RR_LOCAL_MODEM_SNAPSHOT = 1,
   RR_LOCAL_MODEM_START,
   RR_LOCAL_MODEM_STOP,
   RR_LOCAL_MODEM_ROUTE,
   RR_LOCAL_MODEM_TX_ROUTE,
   RR_LOCAL_MODEM_PTT_ROUTE,
   RR_LOCAL_MODEM_SET_TX_ENABLED,
   RR_LOCAL_MODEM_SEND_TEXT,
   RR_LOCAL_MODEM_STREAM_BEGIN,
   RR_LOCAL_MODEM_STREAM_DATA,
   RR_LOCAL_MODEM_STREAM_END,
   RR_LOCAL_MODEM_STREAM_ABORT
} rr_local_modem_action_t;
typedef struct {
   rr_local_modem_action_t action;
   unsigned slot;               /* 1..8; 0 = first free slot on START */
   char source[64];
   char engine[16];             /* psk / olivia / ft8 */
   char option[24];             /* BPSK31 or Olivia tones */
   unsigned value;              /* carrier Hz or Olivia bandwidth */
   bool tx_enabled;             /* Opt-in TX routing; does not authorize PTT */
   char text[256];              /* SEND_TEXT / STREAM_DATA: bounded UTF-8 payload */
} rr_local_modem_request_t;
typedef struct {
   unsigned slot, value;
   bool running, tx_enabled, stream_active, tx_on;
   char source[64], engine[16], option[24];
   guint64 drops, received;
   gint64 tx_started_us; /* Monotonic clock of confirmed key-down; zero in RX */
   char text[RR_LOCAL_MODEM_TEXT];
} rr_local_modem_entry_t;
typedef struct {
   unsigned count, sources;
   rr_local_modem_entry_t entries[RR_LOCAL_MODEM_MAX];
   char source_id[RR_LOCAL_MODEM_STREAMS][64];
   char status[192];
} rr_local_modem_snapshot_t;
/* One immutable inventory used by the core routing graph. RX is currently connected to a decoded PCM source; TX/PTT ports are reserved, not armed. */
typedef struct {
   unsigned slot;
   bool tx_enabled;
   char source[64], engine[16];
   char tx_target[64], ptt_target[64]; /* Planned rig routes, not TX grants. */
} rr_local_modem_route_t;
size_t rr_local_modem_routes(rr_local_modem_route_t *routes, size_t capacity);
void rr_local_modem_init(void);
void rr_local_modem_feed(const char *source, const int16_t *pcm, size_t frames);
void rr_local_modem_shutdown(void);
void rr_local_modem_request(const rr_local_modem_request_t *request);
/* Atomic signal: local modem owns the TX encoder, so mic PCM must be muted. */
bool rr_local_modem_tx_in_progress(void);
bool cmd_local_modem(int argc, char **argv);
#endif

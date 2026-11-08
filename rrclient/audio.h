//
// rrclient/audio.h
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_audio_h)
#define __rrclient_audio_h
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/spectrum.af.h>

extern bool audio_enabled;
extern bool gst_active;

extern bool audio_init(void);
/* Local SDR PCM joins the same RX mixer as remote audio; samples must be 16 kHz mono. */
bool audio_local_rx_start(const char *id);
bool audio_local_rx_push(const char *id, const int16_t *pcm, size_t frames);
void audio_local_rx_stop(const char *id);
extern void audio_tx_free_frame(void);
extern bool ws_audio_init(void);
extern void ws_audio_shutdown(void);
extern bool audio_process_frame(const char *data, size_t len);
#define RRCLIENT_AUDIO_CONTROL_REQUEST "client.audio.control.request"
typedef enum {
   RR_AUDIO_CONTROL_MASTER = 1,
   RR_AUDIO_CONTROL_EAR,
   RR_AUDIO_CONTROL_STREAM
} rr_audio_control_kind_t;
typedef struct {
   rr_audio_control_kind_t kind;
   unsigned channel;
   int percent;
   char stream[64];
   char output;
} rr_audio_control_request_t;
/* Asynchronous GTK -> core dispatch; do not mutate fwdsp or routing state on GTK. */
void audio_request_rx_volume(int percent);
void audio_request_rx_channel_volume(unsigned channel, int percent);
void audio_request_rx_stream_route(const char *id, char output);
extern bool audio_set_rx_volume(int percent);
extern bool audio_set_rx_channel_volume(unsigned channel, int percent);
extern bool audio_set_tx_volume(int percent);
struct rrclient_rx_audio {
   const char *uuid, *codec;
   const char *room;
};
/* Per-server room routing: L/R/S (both ears)/M. Independent of UI selection. */
char audio_room_route(const char *server, const char *room);
bool audio_set_room_route(const char *server, const char *room, char output);
/* Snapshot of all subscribed native RX streams, across connections. Opaque IDs survive codec changes; use only on the client main loop. Returns total count. */
struct rrclient_rx_stream {
   char id[64], server[512], room[128], uuid[64], codec[5];
   char output;
};
size_t audio_rx_streams(struct rrclient_rx_stream *streams, size_t capacity);
/* AF visualization request/reply. Only core reads RX route inventory;
 * GTK receives an owned spectrum sample, never a route pointer. */
#define RRCLIENT_AF_SPECTRUM_REQUEST "client.audio.af-spectrum.request"
#define RRCLIENT_AF_SPECTRUM_RESULT "client.audio.af-spectrum.result"
typedef struct {
   guint64 request_id;
   char server[512], room[128];
} rrclient_af_spectrum_request_t;
typedef struct {
   guint64 request_id;
   bool ready;
   float bins[RR_AF_BINS];      /* fixed-size event payload */
} rrclient_af_spectrum_result_t;
#define RRCLIENT_AUDIO_SNAPSHOT_REQUEST "client.audio.snapshot.request"
#define RRCLIENT_AUDIO_SNAPSHOT_RESULT "client.audio.snapshot.result"
typedef struct {
   uint32_t count;
   struct rrclient_rx_stream streams[];
} rr_audio_stream_snapshot_t;

bool audio_set_rx_stream_route(const char *id, char output);
/* Synchronize this connection's subscribed decoders without stopping muted ones. */
void audio_sync_rx_channels(const struct rrclient_rx_audio *channels, size_t count);

extern bool audio_switch_codec(const char *codec, bool is_tx);
extern void audio_stop_codec(bool is_tx);

extern void try_send_next_frame(rrconn_t *cptr);

#endif // !defined(__rrclient_audio_h)

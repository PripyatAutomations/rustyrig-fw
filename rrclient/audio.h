//
// rrclient/audio.h
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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

extern bool audio_enabled;
extern bool gst_active;

extern bool audio_init(void);
extern void audio_tx_free_frame(void);
extern bool ws_audio_init(void);
extern void ws_audio_shutdown(void);
extern bool audio_process_frame(const char *data, size_t len);
extern bool audio_set_rx_volume(int percent);
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
bool audio_set_rx_stream_route(const char *id, char output);
/* Synchronize this connection's subscribed decoders without stopping muted ones. */
void audio_sync_rx_channels(const struct rrclient_rx_audio *channels, size_t count);

extern bool audio_switch_codec(const char *codec, bool is_tx);
extern void audio_stop_codec(bool is_tx);

extern void try_send_next_frame(rrconn_t *cptr);

#endif // !defined(__rrclient_audio_h)

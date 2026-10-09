//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRCLIENT_MEDIA_H
#define RRCLIENT_MEDIA_H

#include <librrprotocol/rrprotocol.h>

extern void rrclient_media_room_selected(const char *room);
extern void rrclient_media_room_joined(const char *room);
extern void rrclient_media_room_parted(const char *room);
extern bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec);
extern bool rrclient_media_subscribe_single_video(void);
extern const char *rrclient_media_current_codec(bool is_tx);
/* Resolve a subscribed RX audio channel by wire stream id + codec; returns the codec or NULL for stale/foreign frames. */
extern const char *rrclient_media_rx_codec_for_stream(uint8_t stream, const char codec[4]);
/* The server-owned channel selected for the active VFO and direction. */
struct rr_client_media_chan;
extern const struct rr_client_media_chan *rrclient_media_current_channel(bool is_tx);
extern const struct rr_client_media_chan *rrclient_media_codec_target_channel(bool is_tx);
extern bool cmd_rxcodec(int argc, char **args);
extern bool cmd_txcodec(int argc, char **args);

/* Selected room of the automatic local audio pair. */
const char *rrclient_media_active_room(void);

const char *rrclient_media_vfo_uuid(const char *room, char vfo);

#endif

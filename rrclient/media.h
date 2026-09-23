#ifndef RRCLIENT_MEDIA_H
#define RRCLIENT_MEDIA_H

#include <librrprotocol/rrprotocol.h>

extern bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec);
extern const char *rrclient_media_current_codec(bool is_tx);
/* The server-owned channel selected for the active VFO and direction. */
struct rr_client_media_chan;
extern const struct rr_client_media_chan *rrclient_media_current_channel(bool is_tx);
extern const struct rr_client_media_chan *rrclient_media_codec_target_channel(bool is_tx);
extern bool cmd_rxcodec(int argc, char **args);
extern bool cmd_txcodec(int argc, char **args);

#endif

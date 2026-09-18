#ifndef RRCLIENT_MEDIA_H
#define RRCLIENT_MEDIA_H

#include <librrprotocol/rrprotocol.h>

extern bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec);
extern const char *rrclient_media_current_codec(bool is_tx);
extern bool cmd_rxcodec(int argc, char **args);
extern bool cmd_txcodec(int argc, char **args);

#endif

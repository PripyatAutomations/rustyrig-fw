// RustyRig remote RF source and row events (GPL-2.0-only).
#ifndef RRCLIENT_REMOTE_SPECTRUM_H
#define RRCLIENT_REMOTE_SPECTRUM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <rrbridge/spectrum.row.h>
#include <librrprotocol/rrprotocol.h>
#define RR_REMOTE_RF_SOURCES_MAX 32
#define RR_REMOTE_RF_SOURCE_CHANGED "client.remote.rf.changed"
#define RR_REMOTE_RF_ROW_EVENT "client.remote.rf.row"
typedef struct {
   char id[256];   /* profile/channel UUID; stable across stream-id changes */
   char name[96];
   char room[128];
   uint8_t stream;
   bool subscribed, joined;
} rr_remote_rf_source_t;
typedef struct {
   char id[256];
   rr_bridge_spectrum_row row;
} rr_remote_rf_row_t;
/* Thread-safe copies. Do not hand core-owned channel pointers to GTK. */
size_t rrclient_media_remote_rf_snapshot(rr_remote_rf_source_t *out, size_t count);
/* Called only by the native WebSocket core after validating the binframe. */
bool rrclient_media_remote_rf_for_stream(rrconn_t *connection, uint8_t stream, const char codec[4],
                                        char *id, size_t capacity);
#endif

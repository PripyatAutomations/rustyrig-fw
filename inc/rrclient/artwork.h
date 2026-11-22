// Artwork playback from GTK to the client core.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_ARTWORK_H
#define RRCLIENT_ARTWORK_H
#include <stdint.h>
#define RR_ARTWORK_LINE_EVENT "client.artwork.line"
#define RR_ARTWORK_CANCEL_EVENT "client.artwork.cancel"
#define RR_ARTWORK_MAX_LINE 400
typedef struct {
   uint64_t playback_id;
   char profile[128];
   char room[128];
   char text[RR_ARTWORK_MAX_LINE + 1];
} rr_artwork_line_t;
typedef struct {
   uint64_t playback_id;
} rr_artwork_cancel_t;
void rrclient_artwork_register_events(void);
void rrclient_artwork_unregister_events(void);
#endif

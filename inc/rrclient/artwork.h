// Artwork playback from GTK to the client core.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_ARTWORK_H
#define RRCLIENT_ARTWORK_H
#include <stdint.h>
#define RR_ARTWORK_LINE_EVENT "client.artwork.line"
#define RR_ARTWORK_CANCEL_EVENT "client.artwork.cancel"
#define RR_ARTWORK_EDIT_EVENT "client.artwork.edit"
#define RR_ARTWORK_MAX_LINE 400
typedef struct {
   uint64_t playback_id;
   uint32_t interval_ms; /* per-room user-selected pacing */
   char profile[128];
   char room[128];
   char text[RR_ARTWORK_MAX_LINE + 1];
} rr_artwork_line_t;
typedef struct {
   uint64_t playback_id;
} rr_artwork_cancel_t;
typedef struct { char basename[192]; } rr_artwork_edit_t;
void rrclient_artwork_register_events(void);
void rrclient_artwork_unregister_events(void);
#endif

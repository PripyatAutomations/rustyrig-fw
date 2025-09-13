//
// inc/common/codecneg.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if	!defined(__common_codecneg_h)
#define	__common_codecneg_h
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>

struct ws_frame {
  uint8_t magic[2];
  uint8_t *data;
  size_t len;
  enum {
     WS_MEDIA_NONE = 0,
     WS_MEDIA_AUDIO,
     WS_MEDIA_VIDEO,
     WS_MEDIA_FILE
  } data_type;
  struct ws_frame *next;
} __attribute__((packed));

// This is sent as json before any audio samples can be exchanged
struct audio_header {
   char     codec[5];	      // e.g. "mu16"
   uint32_t sample_rate;      // sample rate in hz
   uint8_t  channel_id;	      // Channel ID: 0 for RX, 1 for TX, etc
}  __attribute__((packed));
typedef struct audio_header au_header_t;

typedef struct audio_frame {
   uint8_t     chan_id;        // Channel ID for sample
   uint16_t    data_len;       // # of bytes in the data
   uint8_t    *data;           // raw sample data
   struct audio_frame *next;           // next frame
} au_frame_t;

// ws_send_media_capab: Send our media.capab message. If codecs is not NULL, send overlapping codecs, else send all preferred codecs.
extern const char *media_capab_prepare(const char *codecs);

// codec_filter_common: Return a string with only the codecs supported by both parties or NULL. *MUST* be freed if not NULL!
extern char *codec_filter_common(const char *preferred, const char *available);

#endif	// !defined(__common_codecneg_h)

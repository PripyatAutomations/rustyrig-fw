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
  struct ws_frame *next;
} __attribute__((packed));

// This is sent as json before any audio samples can be exchanged
struct audio_header {
   uint8_t magic[4];	      // e.g. "MU16"
   uint32_t sample_rate;      // sample rate in hz
   uint8_t channel_id;	      // Channel ID: 0 for RX, 1 for TX, etc
}  __attribute__((packed));
typedef struct audio_header au_header_t;

typedef struct audio_frame {
   uint8_t     chan_id;        // Channel ID for sample
   uint16_t    data_len;       // # of bytes in the data
   uint8_t    *data;           // raw sample data
   struct audio_frame *next;           // next frame
} au_frame_t;

struct audio_settings {
    uint32_t		sample_rate;			// sample rate in hz
    bool		active;				// Is the stream active?
};
typedef struct audio_settings audio_settings_t;
extern void audio_tx_free_frame(void);
extern audio_settings_t	au_rx_config, au_tx_config;

// New stuff
extern char *codecneg_send_supported_codecs(void);
extern char *codecneg_send_supported_codecs_plain(void);
extern char *codec_filter_common(const char *preferred, const char *available);
extern int au_codec_start(const char *id, bool is_tx);
extern int au_codec_stop(const char *id, bool is_tx);

#endif	// !defined(__common_codecneg_h)

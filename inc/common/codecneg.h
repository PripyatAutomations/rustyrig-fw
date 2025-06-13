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

enum au_codec {
   AU_CODEC_NONE = 0,			// No codec configured
   AU_CODEC_PCM16,				// 16khz PCM
   AU_CODEC_PCM44,				// 44.1khz PCM
   AU_CODEC_OPUS,				// OPUS
   AU_CODEC_FLAC,				// FLAC
   AU_CODEC_MULAW8,				// u-law PCM @ 8khz
   AU_CODEC_MULAW16				// u-law PCM @ 16khz
};

typedef struct au_codec_mapping {
   enum au_codec  id;
   const char     *magic;
   int            sample_rate;    // -1 if variable
   char           *pipeline;      // optional user-configured pipeline
   int            refcount;       // how many clients are using this
   pid_t          pid;            // optional: encoder/decoder process PID
   int            running;        // flag: pipeline started
} au_codec_mapping_t;

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
    enum au_codec	codec;
    uint32_t		sample_rate;			// sample rate in hz
    bool		active;				// Is the stream active?
};
typedef struct audio_settings audio_settings_t;
extern uint32_t au_codec_get_samplerate(int id);
extern int au_codec_get_id(const char *magic);
extern const char *au_codec_get_magic(int id);
extern au_codec_mapping_t *au_codec_by_id(enum au_codec id);
extern au_codec_mapping_t *au_codec_find_by_magic(const char *magic);
extern void audio_tx_free_frame(void);
extern audio_settings_t	au_rx_config, au_tx_config;

// New stuff
extern char *codecneg_send_supported_codecs(au_codec_mapping_t *codecs);
extern au_codec_mapping_t      au_core_codecs[];

#endif	// !defined(__common_codecneg_h)

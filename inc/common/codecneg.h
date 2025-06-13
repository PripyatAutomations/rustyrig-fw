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
   enum au_codec	id;
   const char		*magic;
   int			sample_rate;		// -1 if variable or set later
   char                 *pipeline;		// contains  *must* be freed
} au_codec_mapping_t;

struct ws_frame {
  uint8_t magic[2];
  uint8_t *data;
  size_t len;
  struct ws_frame *next;
} __attribute__((packed));

struct audio_settings {
    enum au_codec	codec;
    uint32_t		sample_rate;			// sample rate in hz
    bool		active;				// Is the stream active?
};
typedef struct audio_settings audio_settings_t;

#endif	// !defined(__common_codecneg_h)

#if	!defined(__rr_codec_h)
#define	__rr_codec_h

#include "config.h"

#define AUDIO_TYPE_OPUS 0x01
#define AUDIO_TYPE_PCM  0x00

#if	defined(FEATURE_OPUS)
extern bool codec_init(void);
extern void codec_encode_frame(const void *pcm_data, int size);
extern void codec_decode_frame(const unsigned char *opus_data, int opus_size);
extern void codec_fini(void);
extern int codec_get_pcm_frame(void *output, int max_size);

#endif	// defined(FEATURE_OPUS)
#endif	// !defined(__rr_codec_h)

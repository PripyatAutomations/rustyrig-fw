// Client recording codecs: native Ogg/Vorbis and FLAC. GPL-2.0-only.
#ifndef RRCLIENT_RECORDING_CODEC_H
#define RRCLIENT_RECORDING_CODEC_H
#include <glib.h>
#include <stdint.h>
#include <stddef.h>
#include "recordings.h"

typedef struct rr_recording_encoder rr_recording_encoder;
typedef struct rr_recording_decoder rr_recording_decoder;

/* These objects are owned exclusively by their worker thread. */
rr_recording_encoder *rr_recording_encoder_open(const char *ogg_path,
   const char *flac_path, const rr_recording_tags *tags, const char *id);
gboolean rr_recording_encoder_write(rr_recording_encoder *, const int16_t *pcm, size_t frames);
gboolean rr_recording_encoder_close(rr_recording_encoder *);

/* Decoders normalize stereo PCM to 16 kHz, matching the RX engine.
 * read returns frames, 0 on EOF, -1 on decoder failure. */
rr_recording_decoder *rr_recording_decoder_open(const char *path);
gboolean rr_recording_decoder_seek(rr_recording_decoder *, double seconds);
gssize rr_recording_decoder_read(rr_recording_decoder *, int16_t *pcm, size_t frames);
double rr_recording_decoder_duration(rr_recording_decoder *);
void rr_recording_decoder_close(rr_recording_decoder *);
#endif

#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "state.h"
#include "audio.h"
#include "codec.h"
#include <opus/opus.h>

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960  // 20ms frames at 48kHz
#define MAX_FRAME_SIZE 5760 // Max 120ms @ 48kHz
#define OPUS_MAX_PACKET 4000

#if	defined(FEATURE_OPUS)

static OpusEncoder *encoder = NULL;
static OpusDecoder *decoder = NULL;

bool codec_init(void) {
   int error;
   encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error);

   if (error != OPUS_OK) {
      Log(LOG_CRIT, "codec", "Failed to create Opus encoder: %s", opus_strerror(error));
      return true;
   }

   decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &error);
   if (error != OPUS_OK) {
      Log(LOG_CRIT, "codec", "Failed to create Opus decoder: %s", opus_strerror(error));
      return true;
   }
   Log(LOG_INFO, "codec", "OPUS codec initialized");
   return false;
}

void codec_encode_frame(const void *pcm_data, int size) {
   unsigned char opus_data[OPUS_MAX_PACKET];
   int frame_size = size / sizeof(short); // Convert byte size to sample count
   int compressed_size;

   if (!encoder) {
      codec_init();
   }

   compressed_size = opus_encode(encoder, pcm_data, frame_size, opus_data, OPUS_MAX_PACKET);
   if (compressed_size < 0) {
      Log(LOG_DEBUG, "codec", "Opus encoding failed: %s", opus_strerror(compressed_size));
      return;
   }

   Log(LOG_DEBUG, "codec", "Encoded %d bytes -> %d bytes", size, compressed_size);
   // Send 'opus_data' (compressed_size bytes) over WebSocket later
}


void codec_decode_frame(const unsigned char *opus_data, int opus_size) {
   short pcm[MAX_FRAME_SIZE];  // Output PCM buffer
   int frame_size;

   if (!decoder) {
      codec_init();
   }

   // Decode OPUS data into raw PCM samples
   frame_size = opus_decode(decoder, opus_data, opus_size, pcm, FRAME_SIZE, 0);
   if (frame_size < 0) {
      Log(LOG_DEBUG, "codec.noisy", "OPUS decoding failed: %s", opus_strerror(frame_size));
      return;
   }

   // PCM audio is now in `pcm[]` (frame_size samples)
   Log(LOG_INFO, "codec.noisy", "Decoded %d bytes -> %d samples", opus_size, frame_size);

   // TODO: Send `pcm` to PipeWire playback
}

void codec_fini(void) {
   if (decoder) {
      opus_decoder_destroy(decoder);
      decoder = NULL;
   }

   if (encoder) {
      opus_encoder_destroy(encoder);
      encoder = NULL;
   }
}
#endif	// defined(FEATURE_OPUS)

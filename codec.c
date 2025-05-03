//
// Support for OPUS audio codec
//
// We use OPUS because it is supported in all modern browsers.
//
// Eventually, we need to support an unlimited number of audio channels
// so that dual receive, etc is possible
// - Stuff like RX1 on LEFT, RX2 on RIGHT would be pretty fancy
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
#include "au.h"
#include "codec.h"
#include <opus/opus.h>

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define FRAME_SIZE 960  // 20ms frames at 48kHz
#define MAX_FRAME_SIZE 5760 // Max 120ms @ 48kHz
#define OPUS_MAX_PACKET 4000
#define PCM_BUFFER_SIZE (FRAME_SIZE * 10)  // Buffer for 10 frames

#if	defined(FEATURE_OPUS)
static int pcm_buffer_used = 0;
static uint8_t pcm_buffer[PCM_BUFFER_SIZE];
static OpusEncoder *encoder = NULL;
static OpusDecoder *decoder = NULL;

#include "codec.h"

// PipeWire playback stream fetches PCM here
int codec_get_pcm_frame(void *output, int max_size) {
   if (pcm_buffer_used == 0) {
      return 0;  // No audio available
   }

   int to_copy = (pcm_buffer_used < max_size) ? pcm_buffer_used : max_size;
   memcpy(output, pcm_buffer, to_copy);
   pcm_buffer_used -= to_copy;

   return to_copy;
}

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
      Log(LOG_WARN, "codec", "Opus encoding failed: %s", opus_strerror(compressed_size));
      return;
   }

   Log(LOG_CRAZY, "codec", "Encoded %d bytes -> %d bytes", size, compressed_size);

   // XXX: Send the encoded frame via ws
}

// OPUS decoder outputs PCM here
void codec_decode_frame(const uint8_t *data, int len) {
   int decoded_samples = opus_decode(decoder, data, len, 
                                     (opus_int16 *)(pcm_buffer + pcm_buffer_used),
                                     FRAME_SIZE, 0);
   if (decoded_samples > 0) {
      pcm_buffer_used += decoded_samples * sizeof(opus_int16);
   }
   Log(LOG_CRAZY, "codec", "Decoded %d bytes -> %d samples in %d bytes", len, decoded_samples, pcm_buffer_used);
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

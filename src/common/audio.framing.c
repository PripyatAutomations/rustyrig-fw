//
// common/audio.framing.c: Utilities for framing audio in client & server
//
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/debug.h"			// Debug message filtering
#include "common/client-flags.h"

// XXX: We probably need to wrap htons/ntohs for use with win64, confirm this soon
#include <arpa/inet.h>

#define AUDIO_HDR_SIZE 4

size_t pack_audio_frame(uint8_t *out, uint16_t chan_id, uint16_t seq, const void *data, size_t len) {
   uint16_t net_chan = htons(chan_id);
   uint16_t net_seq  = htons(seq);
   memcpy(out,     &net_chan, 2);
   memcpy(out + 2, &net_seq,  2);
   memcpy(out + AUDIO_HDR_SIZE, data, len);
   return AUDIO_HDR_SIZE + len;
}

void unpack_audio_frame(const uint8_t *in, uint16_t *chan_id, uint16_t *seq, const uint8_t **payload) {
   *chan_id = ntohs(*(uint16_t *)(in));
   *seq     = ntohs(*(uint16_t *)(in + 2));
   *payload = in + AUDIO_HDR_SIZE;
}

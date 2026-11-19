// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_FILES_TRANSFER_WIRE_H
#define RR_FILES_TRANSFER_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* FILE binframe codec "fl01". The full binary frame remains on a reliable
 * ordered WebSocket; this is only its bounded payload. Each chunk is bound
 * to a 36-byte UUID transfer ID, an absolute offset and an explicit FIN.
 * No sender, recipient, room or privilege is trusted from the payload. */
#define RR_FILE_FRAME_CODEC "fl01"
#define RR_FILE_FRAME_PREFIX 45u
#define RR_FILE_FRAME_MAX_CHUNK 24576u
#define RR_FILE_FRAME_FIN 0x01u

typedef struct rr_file_chunk {
   char id[37];
   uint64_t offset;
   const uint8_t *bytes;
   size_t length;
   bool fin;
} rr_file_chunk_t;

static inline bool rr_file_uuid_valid(const char *id) {
   if (!id || strlen(id) != 36) return false;
   for (size_t i = 0; i < 36; ++i) {
      const unsigned char c = (unsigned char) id[i];
      if (i == 8 || i == 13 || i == 18 || i == 23) {
         if (c != '-') return false;
      } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
         return false;
      }
   }
   return true;
}

static inline bool rr_file_chunk_pack(uint8_t *out, size_t cap, const char *id,
                                      uint64_t offset, const void *bytes, size_t len,
                                      bool fin, size_t *written) {
   if (!out || !written || !rr_file_uuid_valid(id) ||
       len > RR_FILE_FRAME_MAX_CHUNK || cap < RR_FILE_FRAME_PREFIX ||
       len > cap - RR_FILE_FRAME_PREFIX || (len && !bytes) ||
       offset > UINT64_MAX - len || (!len && !fin)) return false;
   memcpy(out, id, 36);
   for (unsigned i = 0; i < 8; ++i) out[36 + i] = (uint8_t)(offset >> (56 - 8 * i));
   out[44] = fin ? RR_FILE_FRAME_FIN : 0;
   if (len) memcpy(out + RR_FILE_FRAME_PREFIX, bytes, len);
   *written = RR_FILE_FRAME_PREFIX + len;
   return true;
}

static inline bool rr_file_chunk_unpack(rr_file_chunk_t *chunk, const uint8_t *data, size_t len) {
   if (!chunk || !data || len < RR_FILE_FRAME_PREFIX ||
       len - RR_FILE_FRAME_PREFIX > RR_FILE_FRAME_MAX_CHUNK ||
       (data[44] & ~RR_FILE_FRAME_FIN) || (len == RR_FILE_FRAME_PREFIX && !(data[44] & RR_FILE_FRAME_FIN))) return false;
   char id[37];
   memcpy(id, data, 36);
   id[36] = 0;
   if (!rr_file_uuid_valid(id)) return false;
   uint64_t offset = 0;
   for (unsigned i = 0; i < 8; ++i) offset = (offset << 8) | data[36 + i];
   const size_t payload = len - RR_FILE_FRAME_PREFIX;
   if (offset > UINT64_MAX - payload) return false;
   memcpy(chunk->id, id, sizeof(id));
   chunk->offset = offset;
   chunk->bytes = data + RR_FILE_FRAME_PREFIX;
   chunk->length = payload;
   chunk->fin = (data[44] & RR_FILE_FRAME_FIN) != 0;
   return true;
}

/* Offer deadline: room membership at send time never implies consent. */
#define RR_FILE_OFFER_WAIT_SECONDS 30u
static inline bool rr_file_offer_accepts(uint64_t opened_monotonic_s, uint64_t now_monotonic_s) {
   return now_monotonic_s >= opened_monotonic_s &&
          now_monotonic_s - opened_monotonic_s < RR_FILE_OFFER_WAIT_SECONDS;
}
#endif

// SPDX-License-Identifier: GPL-2.0-only
#ifndef RR_SERVER_FILES_LIMITS_H
#define RR_SERVER_FILES_LIMITS_H

#include <stdbool.h>
#include <stdint.h>

/* Never add untrusted byte counts before checking for wraparound. */
static inline bool rr_files_chunk_fits(uint64_t accumulated, uint64_t incoming,
                                      uint64_t maximum, uint64_t chunk_max) {
   return incoming <= chunk_max && accumulated <= maximum &&
          incoming <= maximum - accumulated;
}
#endif

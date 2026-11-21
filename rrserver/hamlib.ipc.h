// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_HAMLIB_IPC_H
#define RRSERVER_HAMLIB_IPC_H

/* Private, per-rig AF_UNIX/SOCK_SEQPACKET protocol. Never expose over TCP.
 * Version changes are incompatible: refuse unknown versions or message types.
 * A socketpair must be created by rrserver before spawning rrhamlib; neither
 * side may accept connections from unrelated processes.
 *
 * This wire protocol does not grant permission to TX by itself.
 * An explicit server opt-in, normal authorization/interlocks and a live
 * independently expiring worker lease are required for PTT_ON.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define RR_HAMLIB_IPC_MAGIC UINT32_C(0x5252484c) /* RRHL */
#define RR_HAMLIB_IPC_VERSION UINT16_C(1)
#define RR_HAMLIB_IPC_MAX_PAYLOAD 4096u

typedef enum {
   RR_HAMLIB_IPC_HELLO = 1,
   RR_HAMLIB_IPC_HELLO_ACK,
   RR_HAMLIB_IPC_REQUEST,
   RR_HAMLIB_IPC_REPLY,
   RR_HAMLIB_IPC_OBSERVATION,
   RR_HAMLIB_IPC_LOG,
   RR_HAMLIB_IPC_RELEASE,
   RR_HAMLIB_IPC_RELEASE_RESULT,
   RR_HAMLIB_IPC_HEARTBEAT,
   RR_HAMLIB_IPC_LEASE_RENEW,
   RR_HAMLIB_IPC_LEASE_EXPIRED
} rr_hamlib_ipc_type_t;

/* Not an on-wire C struct. Encode every field explicitly in network byte
 * order, with no padding; subsequent payload bytes are length bounded.
 * The sequence number correlates responses and prevents stale completions.
 */
/* Fixed-length command payload (12 bytes) and response (16 bytes). VFO
 * indices 0/1 address A/B. All integers are big-endian; no ABI structs on
 * wire. Status 0=success; any other value=error/unknown.
 * TX_ON requires an independently expiring worker-owned lease. The
 * release path is always available, including when TX is disabled.
 */
enum rr_hamlib_ipc_operation {
   RR_HAMLIB_OP_POLL = 1,
   RR_HAMLIB_OP_FREQ_SET,
   RR_HAMLIB_OP_MODE_SET,
   RR_HAMLIB_OP_WIDTH_SET,
   RR_HAMLIB_OP_POWER_SET,
   RR_HAMLIB_OP_SPLIT_SET,
   RR_HAMLIB_OP_SWAP,
   RR_HAMLIB_OP_PTT_ON
};
enum rr_hamlib_ipc_mode {
   RR_HAMLIB_MODE_NONE,
   RR_HAMLIB_MODE_CW,
   RR_HAMLIB_MODE_AM,
   RR_HAMLIB_MODE_LSB,
   RR_HAMLIB_MODE_USB,
   RR_HAMLIB_MODE_FM,
   RR_HAMLIB_MODE_PKTUSB,
   RR_HAMLIB_MODE_PKTLSB
};
#define RR_HAMLIB_IPC_COMMAND_SIZE 12u
#define RR_HAMLIB_IPC_RESULT_SIZE 16u

#define RR_HAMLIB_IPC_HEADER_SIZE 20u
typedef struct {
   uint32_t magic;
   uint16_t version;
   uint16_t type;
   uint32_t sequence;
   uint32_t payload_size;
   uint32_t flags;
} rr_hamlib_ipc_header_t;

static inline bool rr_hamlib_ipc_valid(const rr_hamlib_ipc_header_t *h) {
   return h && h->magic == RR_HAMLIB_IPC_MAGIC &&
      h->version == RR_HAMLIB_IPC_VERSION &&
      h->type >= RR_HAMLIB_IPC_HELLO && h->type <= RR_HAMLIB_IPC_LEASE_EXPIRED &&
      h->payload_size <= RR_HAMLIB_IPC_MAX_PAYLOAD && h->flags == 0;
}

static inline void rr_hamlib_ipc_put16(uint8_t *p, uint16_t v) {
   p[0] = (uint8_t)(v >> 8);
   p[1] = (uint8_t)v;
}
static inline void rr_hamlib_ipc_put32(uint8_t *p, uint32_t v) {
   p[0] = (uint8_t)(v >> 24);
   p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);
   p[3] = (uint8_t)v;
}
static inline uint16_t rr_hamlib_ipc_get16(const uint8_t *p) {
   return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}
static inline uint32_t rr_hamlib_ipc_get32(const uint8_t *p) {
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}
static inline bool rr_hamlib_ipc_encode(uint8_t *dst, size_t size,
   const rr_hamlib_ipc_header_t *h) {
   if (!dst || size < RR_HAMLIB_IPC_HEADER_SIZE || !rr_hamlib_ipc_valid(h))
      return false;
   rr_hamlib_ipc_put32(dst, h->magic);
   rr_hamlib_ipc_put16(dst + 4, h->version);
   rr_hamlib_ipc_put16(dst + 6, h->type);
   rr_hamlib_ipc_put32(dst + 8, h->sequence);
   rr_hamlib_ipc_put32(dst + 12, h->payload_size);
   rr_hamlib_ipc_put32(dst + 16, h->flags);
   return true;
}
static inline bool rr_hamlib_ipc_decode(rr_hamlib_ipc_header_t *h,
   const uint8_t *src, size_t size) {
   if (!h || !src || size < RR_HAMLIB_IPC_HEADER_SIZE)
      return false;
   rr_hamlib_ipc_header_t parsed = {
      .magic = rr_hamlib_ipc_get32(src),
      .version = rr_hamlib_ipc_get16(src + 4),
      .type = rr_hamlib_ipc_get16(src + 6),
      .sequence = rr_hamlib_ipc_get32(src + 8),
      .payload_size = rr_hamlib_ipc_get32(src + 12),
      .flags = rr_hamlib_ipc_get32(src + 16),
   };
   if (!rr_hamlib_ipc_valid(&parsed) ||
       size != RR_HAMLIB_IPC_HEADER_SIZE + parsed.payload_size)
      return false;
   *h = parsed;
   return true;
}
#endif

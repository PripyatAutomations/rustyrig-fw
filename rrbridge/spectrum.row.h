/* Versioned bounded RF spectrum rows from rrbridge to its local host.
 * Deliberately independent of audio and control channels. */
#ifndef RRBRIDGE_SPECTRUM_ROW_H
#define RRBRIDGE_SPECTRUM_ROW_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#define RR_BRIDGE_SPECTRUM_FD 5
#define RR_BRIDGE_SPECTRUM_BINS 1024
#define RR_BRIDGE_SPECTRUM_EVENT "client.local-bridge.spectrum"
typedef struct {
   uint32_t magic, version;
   uint64_t low_hz, high_hz;
   uint32_t sequence, bins;
   uint8_t power[RR_BRIDGE_SPECTRUM_BINS];
} rr_bridge_spectrum_row;
#define RR_BRIDGE_SPECTRUM_MAGIC UINT32_C(0x52525350)
static inline bool rr_bridge_spectrum_valid(const rr_bridge_spectrum_row *r) {
   return r && r->magic == RR_BRIDGE_SPECTRUM_MAGIC && r->version == 1 &&
      r->bins == RR_BRIDGE_SPECTRUM_BINS && r->high_hz > r->low_hz &&
      r->high_hz <= UINT64_C(10000000000);
}
/* Native fixed-layout IPC is same-build, same-host only. Never send over a
 * network; remote spectrum publication requires an independently versioned
 * endianness-defined protocol. */
static inline int rr_bridge_spectrum_send(int fd, const rr_bridge_spectrum_row *r) {
   if (!rr_bridge_spectrum_valid(r)) return -1;
   ssize_t n = send(fd, r, sizeof(*r), MSG_DONTWAIT | MSG_NOSIGNAL);
   if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
   return n == sizeof(*r) ? 1 : -1;
}
static inline int rr_bridge_spectrum_recv(int fd, rr_bridge_spectrum_row *r) {
   ssize_t n = recv(fd, r, sizeof(*r), MSG_DONTWAIT | MSG_TRUNC);
   if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
   return n == sizeof(*r) && rr_bridge_spectrum_valid(r) ? 1 : -1;
}
#endif

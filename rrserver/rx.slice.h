// Shared RX slice identities. No SDR/DSP implementation is required here.
// MIT license; independent of server transport and GTK.
#ifndef RR_RX_SLICE_H
#define RR_RX_SLICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RR_RX_SLICE_NAME_MAX 256
#define RR_RX_SOURCE_MAX 64

typedef enum {
   RR_RX_S16LE = 1,
   RR_RX_F32LE
} rr_rx_sample_format_t;

/* Absolute RF Hz. Canonical names are immutable for the life of a slice. */
typedef struct {
   char source[RR_RX_SOURCE_MAX];   // e.g. rig0; no slashes
   uint64_t center_hz;
   uint32_t bandwidth_hz;
   uint32_t rate;
   rr_rx_sample_format_t format;
   uint8_t channels;
} rr_rx_slice_spec_t;

typedef struct rr_rx_slice rr_rx_slice_t;
typedef struct rr_rx_slices rr_rx_slices_t;

bool rr_rx_slice_name(const rr_rx_slice_spec_t *spec, char *out, size_t size);
bool rr_rx_slice_parse(const char *name, rr_rx_slice_spec_t *out);
rr_rx_slices_t *rr_rx_slices_new(void);
void rr_rx_slices_free(rr_rx_slices_t *registry);
/* Identical normalized specifications return the same object. */
rr_rx_slice_t *rr_rx_slice_acquire(rr_rx_slices_t *registry,
   const rr_rx_slice_spec_t *spec);
void rr_rx_slice_release(rr_rx_slices_t *registry, rr_rx_slice_t *slice);
const char *rr_rx_slice_id(const rr_rx_slice_t *slice);
unsigned rr_rx_slice_subscribers(const rr_rx_slice_t *slice);
size_t rr_rx_slices_count(const rr_rx_slices_t *registry);
#endif

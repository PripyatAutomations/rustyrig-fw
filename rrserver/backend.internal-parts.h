// Internal backend component ownership. Part of RustyRig; MIT license.
#ifndef RR_INTERNAL_PARTS_H
#define RR_INTERNAL_PARTS_H
#include <rrserver/backend.h>
typedef struct rr_internal_parts rr_internal_parts_t;
rr_internal_parts_t *rr_internal_parts_open(rr_backend_t *);
void rr_internal_parts_close(rr_internal_parts_t *);
bool rr_internal_parts_tune(rr_internal_parts_t *, unsigned slot, uint32_t hz);
bool rr_internal_parts_ptt(rr_internal_parts_t *, bool active);
#endif

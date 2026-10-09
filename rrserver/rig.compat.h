// rrserver/rig.compat.h: temporary adapters for default-rig state consumers
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_rig_compat_h)
#define __rrserver_rig_compat_h

#include <stdbool.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.properties.h>

typedef struct rr_cat_compat rr_cat_compat_t;

typedef struct rr_cat_compat_ops {
   bool (*vfo_supported)(rr_server_rig_t *rig, rr_vfo_t vfo, void *user);
   bool (*ptt_get)(rr_server_rig_t *rig, rr_vfo_t vfo, void *user);
   int (*widths_get)(rr_server_rig_t *rig, rr_vfo_t vfo, int *widths, int max, void *user);
   rrconn_t *(*talker_get)(rr_server_rig_t *rig, void *user);
   void *user;
} rr_cat_compat_ops_t;

extern rr_cat_compat_t *rr_cat_compat_new(rr_server_rig_t *rig, const rr_cat_compat_ops_t *ops);
extern void rr_cat_compat_free(rr_cat_compat_t *adapter);

/* Seed a default-rig VFO once before its first backend poll. */
extern void rr_cat_compat_prepare_poll(rr_cat_compat_t *adapter, rr_vfo_t vfo);

/* Return false on success to match the state-send API. */
extern bool rr_cat_compat_publish(rr_cat_compat_t *adapter, rr_vfo_t vfo, int unchanged_interval);
extern bool rr_cat_compat_send_state(rr_cat_compat_t *adapter, rrconn_t *cptr);

#endif // !defined(__rrserver_rig_compat_h)

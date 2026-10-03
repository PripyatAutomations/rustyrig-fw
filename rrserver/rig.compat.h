// rrserver/rig.compat.h: temporary adapters for legacy rig state consumers
#if !defined(__rrserver_rig_compat_h)
#define __rrserver_rig_compat_h

#include <stdbool.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.properties.h>

typedef struct rr_cat_compat rr_cat_compat_t;

typedef struct rr_cat_compat_ops {
   bool (*vfo_supported)(rr_server_rig_t *rig, rr_vfo_t vfo, void *user);
   bool (*ptt_get)(rr_server_rig_t *rig, rr_vfo_t vfo, void *user);
   int (*widths_get)(rr_server_rig_t *rig, rr_vfo_t vfo, int *widths,
      int max, void *user);
   rrconn_t *(*talker_get)(rr_server_rig_t *rig, void *user);
   void *user;
} rr_cat_compat_ops_t;

extern rr_cat_compat_t *rr_cat_compat_new(rr_server_rig_t *rig,
   const rr_cat_compat_ops_t *ops);
extern void rr_cat_compat_free(rr_cat_compat_t *adapter);

/* Return false on success to match the legacy state-send API. */
extern bool rr_cat_compat_publish(rr_cat_compat_t *adapter, rr_vfo_t vfo,
   int unchanged_interval);
extern bool rr_cat_compat_send_state(rr_cat_compat_t *adapter,
   rrconn_t *cptr);

#endif // !defined(__rrserver_rig_compat_h)

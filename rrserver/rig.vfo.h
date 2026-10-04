// rrserver/rig.vfo.h: first-class VFO children owned by server rigs
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_rig_vfo_h)
#define __rrserver_rig_vfo_h

#include <stdbool.h>
#include <stddef.h>

#include <librrprotocol/vfo.h>

struct rr_server_rig;
typedef struct rr_server_vfo rr_server_vfo_t;

typedef enum rr_vfo_lifecycle {
   RR_VFO_PERSISTENT = 0,
   RR_VFO_EPHEMERAL,
} rr_vfo_lifecycle_t;

typedef bool (*rr_server_vfo_iter_fn)(rr_server_vfo_t *vfo, void *user);

extern rr_server_vfo_t *rr_server_vfo_add(struct rr_server_rig *rig,
   const char *uuid, const char *alias, const char *native_id,
   rr_vfo_lifecycle_t lifecycle);
/* Returns false on success, matching the existing registry convention. */
extern bool rr_server_vfo_remove(struct rr_server_rig *rig,
   const char *uuid);
extern rr_server_vfo_t *rr_server_vfo_find_uuid(
   const struct rr_server_rig *rig, const char *uuid);
extern rr_server_vfo_t *rr_server_vfo_find_alias(
   const struct rr_server_rig *rig, const char *alias);
extern size_t rr_server_vfo_count(const struct rr_server_rig *rig);
extern bool rr_server_vfo_foreach(struct rr_server_rig *rig,
   rr_server_vfo_iter_fn callback, void *user);

extern const char *rr_server_vfo_id(const rr_server_vfo_t *vfo);
extern const char *rr_server_vfo_alias(const rr_server_vfo_t *vfo);
extern const char *rr_server_vfo_native_id(const rr_server_vfo_t *vfo);
extern rr_vfo_lifecycle_t rr_server_vfo_lifecycle(
   const rr_server_vfo_t *vfo);
extern struct rr_server_rig *rr_server_vfo_owner(
   const rr_server_vfo_t *vfo);

/* Traditional A-Z backends use this adapter. Native identity, not the
 * display alias, determines the native Hamlib/internal VFO selector. */
extern bool rr_server_vfo_native_index(const rr_server_vfo_t *vfo,
   rr_vfo_t *index);

#endif // !defined(__rrserver_rig_vfo_h)

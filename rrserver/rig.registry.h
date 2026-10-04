// rrserver/rig.registry.h: runtime collection of independently-owned rigs
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_rig_registry_h)
#define __rrserver_rig_registry_h

#include <stdbool.h>
#include <stddef.h>

#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>

typedef struct rr_rig_registry rr_rig_registry_t;
typedef bool (*rr_rig_registry_iter_fn)(rr_server_rig_t *radio, void *user);

extern rr_rig_registry_t *rr_rig_registry_new(void);
extern void rr_rig_registry_free(rr_rig_registry_t *registry);
extern rr_server_rig_t *rr_rig_registry_add(rr_rig_registry_t *registry,
   const char *uuid, const char *alias, const char *name,
   const rr_backend_type_t *backend_type);
extern bool rr_rig_registry_remove(rr_rig_registry_t *registry,
   const char *uuid);
extern rr_server_rig_t *rr_rig_registry_find_uuid(
   const rr_rig_registry_t *registry, const char *uuid);
extern rr_server_rig_t *rr_rig_registry_find_alias(
   const rr_rig_registry_t *registry, const char *alias);
extern rr_server_vfo_t *rr_rig_registry_find_vfo_uuid(
   const rr_rig_registry_t *registry, const char *uuid);
extern const char *rr_rig_registry_alias(const rr_rig_registry_t *registry,
   const rr_server_rig_t *radio);
extern size_t rr_rig_registry_count(const rr_rig_registry_t *registry);
extern bool rr_rig_registry_foreach(rr_rig_registry_t *registry,
   rr_rig_registry_iter_fn callback, void *user);
extern bool rr_rig_registry_set_legacy(rr_rig_registry_t *registry,
   rr_server_rig_t *radio);
extern rr_server_rig_t *rr_rig_registry_legacy(
   const rr_rig_registry_t *registry);

#endif // !defined(__rrserver_rig_registry_h)

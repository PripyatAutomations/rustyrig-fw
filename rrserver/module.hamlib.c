// Optional Hamlib backend module. Backend instances and workers stay server-owned.
// Part of rustyrig-fw; MIT license without mongoose, GPL when built with it.
#include <librustyaxe/core.h>
#include <rrserver/backend.h>
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
extern struct GlobalState rig;
rr_module_event_t modexports[] = {{0}};

bool rr_module_init(void) {
   return !rr_backend_type_register(&rr_backend_hamlib);
}
static bool in_use(rr_server_rig_t *radio, void *unused) {
   (void)unused;
   rr_backend_t *backend = rr_server_rig_backend(radio);
   return backend && backend->type == &rr_backend_hamlib;
}
bool rr_module_can_unload(void) {
   return !rig.rigs || !rr_rig_registry_foreach(rig.rigs, in_use, NULL);
}
void rr_module_shutdown(void) {
   rr_backend_type_unregister(&rr_backend_hamlib);
}

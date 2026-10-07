#include <assert.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrserver/backend.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.vfo.h>
struct GlobalState rig;
bool dying, restarting;
time_t now;
static const char *last_vfo;
static bool last_state;
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {}
static bool ptt(rr_backend_t *backend, rr_server_vfo_t *vfo, bool state) {
   assert(rr_server_rig_backend(rr_server_vfo_owner(vfo)) == backend);
   last_vfo = rr_server_vfo_id(vfo); last_state = state;
   return false;
}
static const rr_backend_funcs_t api = { .ptt_set = ptt };
static const rr_backend_type_t type = { .name = "test", .api = &api };
int main(void) {
   rig.rigs = rr_rig_registry_new();
   rr_server_rig_t *a = rr_rig_registry_add(rig.rigs, "rig-0", "rig0", "rig0", &type);
   rr_server_rig_t *b = rr_rig_registry_add(rig.rigs, "rig-1", "rig1", "rig1", &type);
   assert(a && b);
   assert(rr_server_vfo_add(a, "vfo-0-A", "A", "A", RR_VFO_PERSISTENT));
   assert(rr_server_vfo_add(b, "vfo-1-A", "A", "A", RR_VFO_PERSISTENT));
   assert(rr_server_vfo_add(b, "vfo-1-B", "B", "B", RR_VFO_PERSISTENT));
   assert(!rr_rig_registry_set_default(rig.rigs, a));
   rig.ptt_rig = b;
   assert(!rr_ptt_apply(VFO_A, true));
   assert(last_state && !strcmp(last_vfo, "vfo-1-A"));
   assert(!rr_ptt_apply(VFO_A, false));
   assert(!last_state && !strcmp(last_vfo, "vfo-1-A"));
   active_vfo = VFO_A;
   assert(!rr_ptt_apply(VFO_B, true));
   assert(rr_server_rig_backend(b)->active_vfo == VFO_B && active_vfo == VFO_A);
   assert(!rr_ptt_apply(VFO_B, false));
   assert(rr_server_rig_backend(b)->active_vfo == VFO_B && active_vfo == VFO_A);
   rig.ptt_rig = NULL;
   assert(!rr_ptt_apply(VFO_A, true));
   assert(last_state && !strcmp(last_vfo, "vfo-0-A"));
   rr_rig_registry_free(rig.rigs);
   puts("PASS: PTT apply and release target the selected rig's VFO, preserving default fallback");
}

#include <assert.h>
#include "rrserver/backend.hamlib.c"
struct GlobalState rig;
bool dying, restarting;
time_t now;
static unsigned switches, keys;
static int select_result;
static vfo_t selected;
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {
}
void shutdown_rig(uint32_t code) {
   abort();
}
int rig_set_vfo(RIG *radio, vfo_t vfo) {
   switches++;
   selected = vfo;

   return select_result;
}
int rig_set_ptt(RIG *radio, vfo_t vfo, ptt_t state) {
   keys++;
   assert(vfo == selected);

   return RIG_OK;
}
int main(void) {
   rr_rig_registry_t *registry = rr_rig_registry_new();
   rr_backend_funcs_t api = {
      0
   };
   rr_backend_type_t type = {
      .name = "fixture", .api = &api
   };
   rr_server_rig_t *radio = rr_rig_registry_add(registry, "rig", "rig0", "rig0", &type);
   assert(radio);
   rr_server_vfo_t *a = rr_server_vfo_add(radio, "a", "A", "A", RR_VFO_PERSISTENT);
   rr_server_vfo_t *b = rr_server_vfo_add(radio, "b", "B", "B", RR_VFO_PERSISTENT);
   rr_backend_t *backend = rr_server_rig_backend(radio);
   RIG device = {
      0
   };
   device.state.vfo_list = RIG_VFO_A | RIG_VFO_B;
   hamlib_backend_t data = {
      .rig = &device
   };
   rr_backend_instance_set_data(backend, &data);
   select_result = -1;
   assert(hl_ptt_set(backend, b, true));
   assert(keys == 0 && !data.transmitting);
   select_result = RIG_OK;
   assert(!hl_ptt_set(backend, b, true));
   assert(keys == 1 && data.transmitting && data.tx_vfo == VFO_B);
   unsigned before = switches;
   assert(hl_poll(backend, a) == NULL);
   assert(switches == before); // polling A must not move a transmitting B
   assert(!hl_ptt_set(backend, b, false));
   assert(keys == 2 && !data.transmitting && switches == before);
   rr_backend_instance_set_data(backend, NULL);
   rr_rig_registry_free(registry);
   puts("PASS: Hamlib selects TX before keying, refuses failed selection, and never switches for off-VFO polls or release");
}

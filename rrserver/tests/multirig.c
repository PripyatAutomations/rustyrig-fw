// Focused Phase 2 tests for the runtime rig and backend-instance registry.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.compat.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.registry.h>
#ifdef USE_SQLITE
#include <sqlite3.h>
#endif

time_t now = 100;
bool dying;
bool restarting;
struct GlobalState rig;

void rr_backend_register_builtin_types(void) {
}

#ifdef USE_SQLITE
sqlite3 *masterdb;
char *db_rig_uuid_get_or_create(sqlite3 *db,
   const char *identity_namespace, const char *alias) {
   (void)db; (void)identity_namespace; (void)alias;
   return NULL;
}
#endif

typedef struct fake_backend {
   long observed_frequency;
   long requested_frequency;
   int controls;
   int polls;
   bool fail_poll;
} fake_backend_t;

static int broadcasts;
static long last_broadcast_frequency;

void ws_broadcast_dict(rrconn_t *sender, dict *message, int data_type) {
   (void)sender;
   assert(data_type == WEBSOCKET_OP_TEXT);
   broadcasts++;
   last_broadcast_frequency = dict_get_long(message, "cat.state.freq", -1);
}

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message,
   int data_type) {
   (void)sender; (void)dest; (void)message; (void)data_type;
   return false;
}

static fake_backend_t *fake_data(rr_backend_t *backend) {
   return rr_backend_instance_data(backend);
}

static bool fake_create(rr_backend_t *backend) {
   fake_backend_t *data = calloc(1, sizeof(*data));
   if (!data) return true;
   data->observed_frequency = !strcmp(rr_backend_instance_alias(backend),
      "rig0") ? 14074000 : 7074000;
   rr_backend_instance_set_data(backend, data);
   return false;
}

static void fake_destroy(rr_backend_t *backend) {
   free(fake_data(backend));
   rr_backend_instance_set_data(backend, NULL);
}

static bool fake_vfo_supported(rr_backend_t *backend, rr_vfo_t vfo) {
   (void)backend;
   return vfo == VFO_A;
}

static bool fake_freq_set(rr_backend_t *backend, rr_vfo_t vfo, int freq) {
   fake_backend_t *data = fake_data(backend);
   assert(vfo == VFO_A);
   data->controls++;
   data->requested_frequency = freq;
   return false;
}

static bool fake_mode_set(rr_backend_t *backend, rr_vfo_t vfo,
   rr_mode_t mode) {
   (void)backend; (void)vfo; (void)mode;
   return false;
}

static bool fake_ptt_get(rr_backend_t *backend, rr_vfo_t vfo) {
   (void)backend; (void)vfo;
   return false;
}

static int fake_widths_get(rr_backend_t *backend, rr_vfo_t vfo,
   int *widths, int max) {
   (void)backend; (void)vfo;
   if (!widths || max < 3) return 0;
   widths[0] = 1800; widths[1] = 3000; widths[2] = 3600;
   return 3;
}

static rr_vfo_data_t *fake_poll(rr_backend_t *backend, rr_vfo_t vfo) {
   fake_backend_t *data = fake_data(backend);
   data->polls++;
   if (data->fail_poll) return NULL;
   char property[RR_PROPERTY_NAME_MAX];
   assert(rr_property_vfo_name(property, sizeof(property), 'A',
      RR_PROP_VFO_FREQUENCY));
   dict_value_t value = { .l = data->observed_frequency };
   assert(rr_rig_property_observe(backend->owner, property, VAL_LONG,
      &value) != RR_PROPERTY_ERROR);
   value.s = "USB";
   assert(rr_property_vfo_name(property, sizeof(property), 'A',
      RR_PROP_VFO_MODE));
   assert(rr_rig_property_observe(backend->owner, property, VAL_STR,
      &value) != RR_PROPERTY_ERROR);
   value.i = 3000;
   assert(rr_property_vfo_name(property, sizeof(property), 'A',
      RR_PROP_VFO_WIDTH));
   assert(rr_rig_property_observe(backend->owner, property, VAL_INT,
      &value) != RR_PROPERTY_ERROR);

   rr_vfo_data_t *result = calloc(1, sizeof(*result));
   if (!result) return NULL;
   result->id = vfo;
   result->freq = data->observed_frequency;
   result->mode = MODE_USB;
   result->width = 3000;
   return result;
}

static const rr_backend_funcs_t fake_api = {
   .create = fake_create,
   .destroy = fake_destroy,
   .poll_state = fake_poll,
   .vfo_supported = fake_vfo_supported,
   .freq_set = fake_freq_set,
   .mode_set = fake_mode_set,
   .ptt_get = fake_ptt_get,
   .widths_get = fake_widths_get,
};

const rr_backend_type_t rr_backend_internal = {
   .name = "fake",
   .description = "test backend",
   .uses_property_state = true,
   .api = &fake_api,
};

#ifdef USE_HAMLIB
const rr_backend_type_t rr_backend_hamlib = {
   .name = "fake-hamlib",
   .description = "test placeholder",
   .uses_property_state = true,
   .api = &fake_api,
};
#endif

static void ignore_property_event(const char *event, const char *data,
   rrconn_t *client, void *user) {
   (void)event; (void)data; (void)client; (void)user;
}

static bool compat_supported(rr_server_rig_t *radio, rr_vfo_t vfo,
   void *user) {
   assert(radio == user);
   return vfo == VFO_A;
}

static bool compat_ptt(rr_server_rig_t *radio, rr_vfo_t vfo, void *user) {
   assert(radio == user);
   (void)vfo;
   return false;
}

static int compat_widths(rr_server_rig_t *radio, rr_vfo_t vfo,
   int *widths, int max, void *user) {
   assert(radio == user);
   (void)vfo;
   if (max < 3) return 0;
   widths[0] = 1800; widths[1] = 3000; widths[2] = 3600;
   return 3;
}

static void define_properties(rr_server_rig_t *radio) {
   assert(!rr_rig_define_vfo_properties(radio, 'A'));
}

static long read_frequency(rr_server_rig_t *radio) {
   char property[RR_PROPERTY_NAME_MAX];
   rr_property_snapshot_t snapshot = { 0 };
   assert(rr_property_vfo_name(property, sizeof(property), 'A',
      RR_PROP_VFO_FREQUENCY));
   assert(rr_rig_property_read(radio, property, &snapshot));
   assert(snapshot.known);
   return snapshot.value.l;
}

int main(void) {
   cfg = dict_new();
   default_cfg = dict_new();
   assert(cfg && default_cfg);
   dict_add(cfg, "rig.vfos", "1");
   dict_add(cfg, "backend.state-interval", "15");
   event_init();
   event_on(RR_PROPERTY_CHANGED_EVENT, ignore_property_event, NULL);
   assert(!rr_backend_type_register(&rr_backend_internal));
   assert(rr_backend_type_find("fake") == &rr_backend_internal);

   rig.rigs = rr_rig_registry_new();
   assert(rig.rigs);
   const char *uuid_a = "11111111-1111-4111-8111-111111111111";
   const char *uuid_b = "22222222-2222-4222-8222-222222222222";
   rr_server_rig_t *radio_a = rr_rig_registry_add(rig.rigs, uuid_a,
      "rig0", "Radio A", &rr_backend_internal);
   rr_server_rig_t *radio_b = rr_rig_registry_add(rig.rigs, uuid_b,
      "rig1", "Radio B", &rr_backend_internal);
   assert(radio_a && radio_b && radio_a != radio_b);
   assert(rr_rig_registry_count(rig.rigs) == 2);
   assert(rr_rig_registry_find_uuid(rig.rigs, uuid_a) == radio_a);
   assert(rr_rig_registry_find_uuid(rig.rigs, uuid_b) == radio_b);
   assert(rr_rig_registry_find_alias(rig.rigs, "rig0") == radio_a);
   assert(rr_rig_registry_find_alias(rig.rigs, "rig1") == radio_b);
   assert(strcmp(rr_server_rig_id(radio_a), rr_server_rig_id(radio_b)));
   assert(rr_server_rig_backend(radio_a) != rr_server_rig_backend(radio_b));
   assert(rr_server_rig_backend(radio_a)->type ==
      rr_server_rig_backend(radio_b)->type);
   assert(fake_data(rr_server_rig_backend(radio_a)) !=
      fake_data(rr_server_rig_backend(radio_b)));
   define_properties(radio_a);
   define_properties(radio_b);

   assert(!rr_rig_registry_set_legacy(rig.rigs, radio_a));
   assert(rr_rig_registry_legacy(rig.rigs) == radio_a);
   assert(rr_rig_registry_remove(rig.rigs, uuid_a));
   rr_cat_compat_ops_t compat_ops = {
      .vfo_supported = compat_supported,
      .ptt_get = compat_ptt,
      .widths_get = compat_widths,
      .user = radio_a,
   };
   rig.legacy_cat = rr_cat_compat_new(radio_a, &compat_ops);
   assert(rig.legacy_cat);

   /* The non-legacy rig observes independently and cannot publish cat.state. */
   assert(!rr_backend_poll_rig(radio_b, VFO_A));
   assert(read_frequency(radio_b) == 7074000);
   assert(broadcasts == 0);
   assert(!rr_backend_poll_rig(radio_a, VFO_A));
   assert(read_frequency(radio_a) == 14074000);
   assert(read_frequency(radio_b) == 7074000);
   assert(broadcasts == 1);
   assert(last_broadcast_frequency == 14074000);

   char frequency[RR_PROPERTY_NAME_MAX];
   assert(rr_property_vfo_name(frequency, sizeof(frequency), 'A',
      RR_PROP_VFO_FREQUENCY));
   rr_control_request_t request_a = {
      .rig = radio_a, .property = frequency, .value_type = VAL_LONG,
      .value.l = 14200000, .source = "test",
   };
   rr_control_request_t request_b = {
      .rig = radio_b, .property = frequency, .value_type = VAL_LONG,
      .value.l = 7100000, .source = "test",
   };
   assert(rr_rig_control(&request_a) == RR_CONTROL_OK);
   assert(fake_data(rr_server_rig_backend(radio_a))->controls == 1);
   assert(fake_data(rr_server_rig_backend(radio_b))->controls == 0);
   assert(rr_rig_control(&request_b) == RR_CONTROL_OK);
   assert(fake_data(rr_server_rig_backend(radio_a))->controls == 1);
   assert(fake_data(rr_server_rig_backend(radio_b))->controls == 1);
   assert(fake_data(rr_server_rig_backend(radio_a))->requested_frequency ==
      14200000);
   assert(fake_data(rr_server_rig_backend(radio_b))->requested_frequency ==
      7100000);
   /* Successful controls do not overwrite observations. */
   assert(read_frequency(radio_a) == 14074000);
   assert(read_frequency(radio_b) == 7074000);

   fake_backend_t *data_a = fake_data(rr_server_rig_backend(radio_a));
   fake_backend_t *data_b = fake_data(rr_server_rig_backend(radio_b));
   data_a->polls = data_b->polls = 0;
   data_b->fail_poll = true;
   assert(rr_backend_poll_all());
   assert(data_a->polls == 1);
   assert(data_b->polls == 1);

   rr_cat_compat_free(rig.legacy_cat);
   rig.legacy_cat = NULL;
   assert(!rr_rig_registry_remove(rig.rigs, uuid_b));
   assert(rr_rig_registry_count(rig.rigs) == 1);
   assert(rr_rig_registry_find_uuid(rig.rigs, uuid_a) == radio_a);
   assert(read_frequency(radio_a) == 14074000);
   assert(rr_rig_registry_find_uuid(rig.rigs, uuid_b) == NULL);

   rr_rig_registry_free(rig.rigs);
   rig.rigs = NULL;
   event_shutdown();
   dict_free(cfg); cfg = NULL;
   dict_free(default_cfg); default_cfg = NULL;
   puts("PASS: independent UUID-addressed rigs and backend instances");
   return 0;
}

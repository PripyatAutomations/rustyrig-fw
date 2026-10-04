// Regression tests for explicit per-rig property ownership and state.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.compat.h>
#include <rrserver/rig.properties.h>
#include <rrserver/rig.vfo.h>

time_t now = 100;
bool dying;
bool restarting;

static int property_events;
static int backend_requests;
static bool backend_should_fail;
static char last_control_property[RR_PROPERTY_NAME_MAX];
static long last_control_frequency;
static int broadcasts;
static int direct_sends;
static dict *last_cat_state;
static rrconn_t talker = { .chatname = "W1TEST" };

static bool count_vfo(rr_server_vfo_t *vfo, void *user) {
   assert(vfo);
   (*(size_t *)user)++;
   return false;
}

void ws_broadcast_dict(rrconn_t *sender, dict *d, int data_type) {
   (void)sender;
   assert(data_type == WEBSOCKET_OP_TEXT);
   broadcasts++;
   if (last_cat_state) {
      dict_free(last_cat_state);
   }
   last_cat_state = dict_new();
   assert(last_cat_state && !dict_merge(last_cat_state, d));
}

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int data_type) {
   (void)sender;
   assert(dest);
   assert(data_type == WEBSOCKET_OP_TEXT);
   direct_sends++;
   if (last_cat_state) {
      dict_free(last_cat_state);
   }
   last_cat_state = dict_new();
   assert(last_cat_state && !dict_merge(last_cat_state, d));
   return false;
}

static bool fake_vfo_supported(rr_server_rig_t *rig, rr_vfo_t vfo,
   void *user) {
   assert(rig == user);
   return vfo == VFO_A || vfo == VFO_B;
}

static bool fake_ptt_get(rr_server_rig_t *rig, rr_vfo_t vfo, void *user) {
   assert(rig == user);
   (void)vfo;
   return false;
}

static int fake_widths_get(rr_server_rig_t *rig, rr_vfo_t vfo, int *widths,
   int max, void *user) {
   assert(rig == user);
   (void)vfo;
   assert(max >= 3);
   widths[0] = 1800;
   widths[1] = 3000;
   widths[2] = 3600;
   return 3;
}

static rrconn_t *fake_talker_get(rr_server_rig_t *rig, void *user) {
   assert(rig == user);
   return &talker;
}

static void on_property_changed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   (void)cptr;
   (void)user;
   assert(strcmp(event, RR_PROPERTY_CHANGED_EVENT) == 0);
   assert(data);
   dict *payload = json2dict(data);
   assert(payload);
   assert(dict_get(payload, "rig.id", NULL) != NULL);
   assert(dict_get(payload, "property.name", NULL) != NULL);
   dict_free(payload);
   property_events++;
}

static rr_control_result_t fake_control(const rr_control_request_t *request,
   void *user) {
   rr_server_rig_t *expected = user;
   assert(request);
   assert(request->rig == expected);
   assert(request->source && strcmp(request->source, "test") == 0);
   assert(request->vfo == rr_server_vfo_find_alias(expected, "A"));
   assert(request->value_type == VAL_LONG);
   backend_requests++;
   snprintf(last_control_property, sizeof(last_control_property), "%s",
      request->property);
   last_control_frequency = request->value.l;
   return backend_should_fail ? RR_CONTROL_BACKEND_FAILED : RR_CONTROL_OK;
}

static void define_fake_rig(rr_server_rig_t *rig) {
   char uuid[64];
   snprintf(uuid, sizeof(uuid), "%s-vfo-a", rr_server_rig_id(rig));
   assert(rr_server_vfo_add(rig, uuid, "A", "A", RR_VFO_PERSISTENT));
   snprintf(uuid, sizeof(uuid), "%s-vfo-b", rr_server_rig_id(rig));
   assert(rr_server_vfo_add(rig, uuid, "B", "B", RR_VFO_PERSISTENT));
   assert(!rr_rig_define_vfo_properties(rig, 'A'));
   assert(!rr_rig_define_vfo_properties(rig, 'B'));

   rr_property_descriptor_t ptt = {
      .name = RR_PROP_PTT,
      .type = VAL_BOOL,
      .readable = true,
      .writable = true,
   };
   rr_property_descriptor_t signal = {
      .name = RR_PROP_SIGNAL,
      .type = VAL_INT,
      .readable = true,
      .writable = false,
      .unit = "dB",
   };
   assert(!rr_rig_property_define(rig, &ptt));
   assert(!rr_rig_property_define(rig, &signal));
}

static rr_property_snapshot_t read_property(rr_server_rig_t *rig,
   const char *name) {
   rr_property_snapshot_t snapshot = { 0 };
   assert(rr_rig_property_read(rig, name, &snapshot));
   return snapshot;
}

int main(void) {
   event_init();
   event_on(RR_PROPERTY_CHANGED_EVENT, on_property_changed, NULL);

   rr_server_rig_t *rig0 = rr_server_rig_new("rig0", "FT-891 A");
   rr_server_rig_t *rig1 = rr_server_rig_new("rig1", "FT-891 B");
   assert(rig0 && rig1 && rig0 != rig1);
   assert(strcmp(rr_server_rig_id(rig0), "rig0") == 0);
   assert(strcmp(rr_server_rig_name(rig1), "FT-891 B") == 0);
   define_fake_rig(rig0);
   define_fake_rig(rig1);
   assert(rr_server_vfo_count(rig0) == 2);
   size_t iterated = 0;
   assert(!rr_server_vfo_foreach(rig0, count_vfo, &iterated));
   assert(iterated == 2);
   rr_server_vfo_t *rig0_a = rr_server_vfo_find_alias(rig0, "A");
   rr_server_vfo_t *rig0_b = rr_server_vfo_find_alias(rig0, "B");
   rr_server_vfo_t *rig1_a = rr_server_vfo_find_alias(rig1, "A");
   assert(rig0_a && rig0_b && rig1_a);
   assert(rr_server_vfo_find_uuid(rig0, rr_server_vfo_id(rig0_a)) ==
      rig0_a);
   assert(strcmp(rr_server_vfo_id(rig0_a), rr_server_vfo_id(rig0_b)));
   assert(strcmp(rr_server_vfo_id(rig0_a), rr_server_vfo_id(rig1_a)));
   assert(rr_server_vfo_owner(rig0_a) == rig0);
   assert(rr_server_vfo_lifecycle(rig0_a) == RR_VFO_PERSISTENT);
   assert(!strcmp(rr_server_vfo_native_id(rig0_a), "A"));

   rr_server_vfo_t *ephemeral = rr_server_vfo_add(rig1,
      "ephemeral-vfo-c", "C", "C", RR_VFO_EPHEMERAL);
   assert(ephemeral);
   assert(rr_server_vfo_lifecycle(ephemeral) == RR_VFO_EPHEMERAL);
   assert(!rr_server_vfo_remove(rig1, rr_server_vfo_id(ephemeral)));
   assert(!rr_server_vfo_find_alias(rig1, "C"));

   char a_freq[RR_PROPERTY_NAME_MAX];
   char a_mode[RR_PROPERTY_NAME_MAX];
   char b_freq[RR_PROPERTY_NAME_MAX];
   char b_mode[RR_PROPERTY_NAME_MAX];
   char b_width[RR_PROPERTY_NAME_MAX];
   assert(rr_property_vfo_name(a_freq, sizeof(a_freq), 'A',
      RR_PROP_VFO_FREQUENCY));
   assert(rr_property_vfo_name(a_mode, sizeof(a_mode), 'A',
      RR_PROP_VFO_MODE));
   assert(rr_property_vfo_name(b_freq, sizeof(b_freq), 'B',
      RR_PROP_VFO_FREQUENCY));
   assert(rr_property_vfo_name(b_mode, sizeof(b_mode), 'B',
      RR_PROP_VFO_MODE));
   assert(rr_property_vfo_name(b_width, sizeof(b_width), 'B',
      RR_PROP_VFO_WIDTH));

   dict_value_t value = { .l = 14074000 };
   assert(rr_rig_property_observe(rig0, a_freq, VAL_LONG, &value) ==
      RR_PROPERTY_CHANGED);
   rr_property_snapshot_t canonical = { 0 };
   assert(rr_vfo_property_read(rig0_a, RR_PROP_VFO_FREQUENCY, &canonical));
   assert(canonical.known && canonical.value.l == 14074000);
   value.s = "USB";
   assert(rr_rig_property_observe(rig0, a_mode, VAL_STR, &value) ==
      RR_PROPERTY_CHANGED);
   value.l = 7074000;
   assert(rr_rig_property_observe(rig0, b_freq, VAL_LONG, &value) ==
      RR_PROPERTY_CHANGED);
   value.s = "USB";
   assert(rr_rig_property_observe(rig0, b_mode, VAL_STR, &value) ==
      RR_PROPERTY_CHANGED);
   value.i = 0;
   assert(rr_rig_property_observe(rig0, RR_PROP_PTT, VAL_BOOL, &value) ==
      RR_PROPERTY_CHANGED);

   /* An independently created rig must not see rig0's observations. */
   rr_property_snapshot_t snapshot = read_property(rig1, a_freq);
   assert(!snapshot.observed);
   assert(!snapshot.known);
   assert(!snapshot.available);

   /* A failed first observation is distinct from never having tried. */
   assert(rr_rig_property_unavailable(rig1, b_width) ==
      RR_PROPERTY_CHANGED);
   snapshot = read_property(rig1, b_width);
   assert(snapshot.observed && !snapshot.known && !snapshot.available);

   value.l = 10136000;
   assert(rr_rig_property_observe(rig1, a_freq, VAL_LONG, &value) ==
      RR_PROPERTY_CHANGED);
   snapshot = read_property(rig0, a_freq);
   assert(snapshot.observed && snapshot.known && snapshot.available);
   assert(snapshot.value_type == VAL_LONG);
   assert(snapshot.value.l == 14074000);
   snapshot = read_property(rig1, a_freq);
   assert(snapshot.value.l == 10136000);

   /* Canonical VFO properties and legacy paths resolve to one value/version. */
   value.l = 10137000;
   assert(rr_vfo_property_observe(rig1_a, RR_PROP_VFO_FREQUENCY, VAL_LONG,
      &value) == RR_PROPERTY_CHANGED);
   canonical = read_property(rig1, a_freq);
   assert(canonical.value.l == 10137000);
   rr_property_snapshot_t direct = { 0 };
   assert(rr_vfo_property_read(rig1_a, RR_PROP_VFO_FREQUENCY, &direct));
   assert(direct.version == canonical.version);
   snapshot = canonical;

   /* Re-observing an identical value is not a state change. */
   uint64_t version = snapshot.version;
   int events = property_events;
   assert(rr_rig_property_observe(rig1, a_freq, VAL_LONG, &value) ==
      RR_PROPERTY_UNCHANGED);
   snapshot = read_property(rig1, a_freq);
   assert(snapshot.version == version);
   assert(property_events == events);

   /* An unavailable read retains its known value without claiming freshness. */
   now++;
   assert(rr_rig_property_unavailable(rig0, a_freq) == RR_PROPERTY_CHANGED);
   snapshot = read_property(rig0, a_freq);
   assert(snapshot.observed && snapshot.known && !snapshot.available);
   assert(snapshot.value.l == 14074000);
   assert(snapshot.changed_at == now);
   assert(rr_rig_property_unavailable(rig0, a_freq) ==
      RR_PROPERTY_UNCHANGED);

   /* Zero and false are real known values, never availability sentinels. */
   value.i = 0;
   assert(rr_rig_property_observe(rig0, RR_PROP_SIGNAL, VAL_INT, &value) ==
      RR_PROPERTY_CHANGED);
   snapshot = read_property(rig0, RR_PROP_SIGNAL);
   assert(snapshot.known && snapshot.available && snapshot.value.i == 0);
   snapshot = read_property(rig0, RR_PROP_PTT);
   assert(snapshot.known && snapshot.available && !snapshot.value.i);

   /* String observations are copied and outlive the caller's buffer. */
   char mode[] = "LSB";
   value.s = mode;
   assert(rr_rig_property_observe(rig1, a_mode, VAL_STR, &value) ==
      RR_PROPERTY_CHANGED);
   mode[0] = 'X';
   snapshot = read_property(rig1, a_mode);
   assert(strcmp(snapshot.value.s, "LSB") == 0);

   /* Pointer values have deliberately unspecified ownership and are rejected. */
   rr_property_descriptor_t pointer_property = {
      .name = "test.pointer",
      .type = VAL_PTR,
      .readable = true,
   };
   assert(rr_rig_property_define(rig0, &pointer_property));

   /* Control validates centrally and never mutates authoritative observations. */
   rr_server_rig_set_control_handler(rig0, fake_control, rig0);
   rr_control_request_t request = {
      .rig = rig0,
      .property = a_freq,
      .value_type = VAL_LONG,
      .value.l = 14200000,
      .source = "test",
   };
   backend_should_fail = true;
   assert(rr_rig_control(&request) == RR_CONTROL_BACKEND_FAILED);
   snapshot = read_property(rig0, a_freq);
   assert(snapshot.value.l == 14074000);
   backend_should_fail = false;
   assert(rr_rig_control(&request) == RR_CONTROL_OK);
   snapshot = read_property(rig0, a_freq);
   assert(snapshot.value.l == 14074000);
   assert(backend_requests == 2);
   assert(strcmp(last_control_property, RR_PROP_VFO_FREQUENCY) == 0);
   assert(last_control_frequency == 14200000);

   request.value_type = VAL_INT;
   assert(rr_rig_control(&request) == RR_CONTROL_TYPE_MISMATCH);
   assert(backend_requests == 2);

   /* cat.state remains a wire adapter above generic observed state. */
   vfos[VFO_A].freq = 1;
   vfos[VFO_A].mode = MODE_AM;
   vfos[VFO_A].width = 3000;
   vfos[VFO_A].power = 0;
   active_vfo = VFO_A;
   rr_cat_compat_ops_t compat_ops = {
      .vfo_supported = fake_vfo_supported,
      .ptt_get = fake_ptt_get,
      .widths_get = fake_widths_get,
      .talker_get = fake_talker_get,
      .user = rig0,
   };
   rr_cat_compat_t *compat = rr_cat_compat_new(rig0, &compat_ops);
   assert(compat);

   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 1);
   assert(last_cat_state);
   assert(strcmp(dict_get(last_cat_state, "msg.type", ""), "cat") == 0);
   assert(strcmp(dict_get(last_cat_state, "cat.state.vfo", ""), "A") == 0);
   assert(strcmp(dict_get(last_cat_state, "cat.state.mode", ""), "USB") == 0);
   assert(dict_get_long(last_cat_state, "cat.state.freq", -1) == 14074000);
   assert(dict_get_int(last_cat_state, "cat.state.width", -1) == 3000);
   assert(!dict_get_bool(last_cat_state, "cat.state.ptt", true));
   assert(strcmp(dict_get(last_cat_state, "cat.user", ""), "W1TEST") == 0);

   /* An unchanged poll inside the interval produces no redundant broadcast. */
   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 1);

   value.l = 14100000;
   assert(rr_rig_property_observe(rig0, a_freq, VAL_LONG, &value) ==
      RR_PROPERTY_CHANGED);
   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 2);
   assert(dict_get_long(last_cat_state, "cat.state.freq", -1) == 14100000);

   value.s = "LSB";
   assert(rr_rig_property_observe(rig0, a_mode, VAL_STR, &value) ==
      RR_PROPERTY_CHANGED);
   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 3);
   assert(strcmp(dict_get(last_cat_state, "cat.state.mode", ""), "LSB") == 0);

   /* A failed read keeps the last value and does not create wire churn. */
   assert(rr_rig_property_unavailable(rig0, a_freq) == RR_PROPERTY_CHANGED);
   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 3);

   /* A known zero remains a real value at the compatibility boundary. */
   value.l = 0;
   assert(rr_rig_property_observe(rig0, a_freq, VAL_LONG, &value) ==
      RR_PROPERTY_CHANGED);
   assert(!rr_cat_compat_publish(compat, VFO_A, 15));
   assert(broadcasts == 4);
   assert(dict_get_long(last_cat_state, "cat.state.freq", -1) == 0);

   rrconn_t new_client = { 0 };
   assert(!rr_cat_compat_send_state(compat, &new_client));
   assert(direct_sends == 2); /* VFO A and B are both supported. */

   rr_cat_compat_free(compat);
   dict_free(last_cat_state);
   last_cat_state = NULL;

   const char *removed_uuid = rr_server_vfo_id(rig0_b);
   char removed_copy[64];
   snprintf(removed_copy, sizeof(removed_copy), "%s", removed_uuid);
   assert(!rr_server_vfo_remove(rig0, removed_copy));
   assert(rr_server_vfo_count(rig0) == 1);
   assert(rr_server_vfo_find_alias(rig0, "A") == rig0_a);
   assert(!rr_server_vfo_find_uuid(rig0, removed_copy));

   rr_server_rig_free(rig1);
   rr_server_rig_free(rig0);
   event_shutdown();
   puts("PASS: explicit per-rig typed property ownership and control");
   return 0;
}

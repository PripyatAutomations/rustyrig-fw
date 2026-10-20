// Safety lockouts must inhibit key-down while always allowing key-up.
#include <assert.h>
#include <rrserver/supervision.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <rrserver/ptt.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/backend.worker.h>
#include <rrserver/database.h>
#include <rrserver/media.h>
#include <rrserver/au.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.properties.h>
struct GlobalState rig;
time_t now, ptt_tot_time;
bool dying, restarting;
sqlite3 *masterdb;
static unsigned applied;
const char *rr_rig_registry_public_alias(const rr_rig_registry_t *registry, const rr_server_rig_t *radio) {
   return "rig0";
}
const char *rrserver_relay_client_server(const rrconn_t *client) {
   return "home-server";
}
rr_server_rig_t *rr_rig_registry_default(const rr_rig_registry_t *registry) {
   return (rr_server_rig_t *) 1;
}
rr_server_vfo_t *rr_server_vfo_find_alias(const rr_server_rig_t *radio, const char *alias) {
   return *alias == 'A' ? (rr_server_vfo_t *) 1 : NULL;
}
bool rr_vfo_property_read(const rr_server_vfo_t *vfo, const char *name, rr_property_snapshot_t *state) {
   return false;
}
bool rr_backend_vfo_supported(rr_server_rig_t *radio, rr_server_vfo_t *object) {
   return true;
}

static bool use_worker;
static rr_backend_t fixture_backend = {
   .owner = (rr_server_rig_t *) 1, .worker = (struct rr_backend_worker *)1
};

static rr_backend_completion_fn queued_completion;
static void *queued_user;
bool rr_backend_worker_submit(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_backend_operation_t operation, dict_value_t value, rr_backend_completion_fn
   complete, void *user) {
   assert(use_worker && backend == &fixture_backend && operation == RR_BACKEND_PTT && !value.i);
   assert(complete);
   queued_completion = complete;
   queued_user = user;

   return false;
}
rr_backend_t *rr_backend_for_ptt(void) {
   return use_worker ? &fixture_backend : NULL;
}

static bool fail_release;
bool rr_ptt_apply(rr_vfo_t vfo, bool state) {
   assert(vfo == VFO_A && !state);
   applied++;

   return fail_release;
}
float rr_get_power(rr_vfo_t vfo) {
   return 0;
}
uint16_t rr_get_width(rr_vfo_t vfo) {
   return 0;
}
rr_mode_t rr_get_mode(rr_vfo_t vfo) {
   return MODE_NONE;
}
const char *rr_get_mode_str(rr_vfo_t vfo) {
   return NULL;
}
const char *rr_rig_registry_room(const rr_rig_registry_t *r, const rr_server_rig_t *v) {
   return "#test-rig0";
}
bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker) {
   return true;
}
void rrserver_media_record_ptt(rr_vfo_t vfo, bool state, rrconn_t *talker, const char *id) {
}
bool au_recording_generate_id(char *buffer, size_t len) {
   return false;
}
int db_ptt_start(sqlite3 *db, const char *user, const char *vfo, double frequency, const char *mode, int width, float power, const char *file, const char *id,
   const char *server) {
   assert(false);

   return -1;
}
void rrserver_supervision_snapshot(rrconn_t *client, const char *room, struct rr_supervision_attribution *out) {
   memset(out, 0, sizeof(*out));
}

int db_ptt_start_supervised(sqlite3 *db, const char *user, const char *vfo, double frequency, const char *mode, int width, float power, const char *file, const
   char *id, const char *server, const char *ticket, const char *elmers) {
   assert(false);               /* Release-only fixture must never create an accounting row. */

   return -1;
}
bool db_ptt_stop(sqlite3 *db, int id, int *duration, const char *reason) {
   assert(false);

   return false;
}
int db_quota_get(sqlite3 *db, const char *user) {
   assert(false);

   return 0;
}
bool db_quota_spend(sqlite3 *db, const char *user, int secs) {
   assert(false);

   return false;
}
bool db_send_notice(rrconn_t *client, const char *type, const char *text) {
   assert(false);

   return false;
}
int main(void) {
   cfg = dict_new();
   rig.tx_blocked = true;
   global_tot_time = 100;
   assert(!rr_ptt_set_reason(VFO_A, true, "blocked"));
   assert(!applied && global_tot_time == 100);
   assert(!rr_ptt_set_reason(VFO_A, false, "lockout-release"));
   assert(applied == 1 && !global_tot_time);
   global_tot_time = 200;
   fail_release = true;
   assert(rr_ptt_request(VFO_A, false, "backend-failure"));
   assert(applied == 2 && global_tot_time == 200);
   fail_release = false;
   assert(!rr_ptt_request(VFO_A, false, "retry"));
   assert(applied == 3 && !global_tot_time);
   event_init();
   rr_ptt_register_accounting_events();
   event_emit("accounting.write.failed", NULL, "advisory-test");
   assert(!rr_ptt_accounting_locked());
   dict_add(cfg, "accounting.strict", "true");
   event_emit("accounting.write.failed", NULL, "strict-test");
   assert(rr_ptt_accounting_locked());
   rr_ptt_set_blocked(false);
   dict_add(cfg, "accounting.strict", "false");
   assert(rr_ptt_check_blocked());  // neither admin unblock nor rehash clears latch
   assert(rr_ptt_request(VFO_A, true, "locked"));
   assert(!rr_ptt_request(VFO_A, false, "safe-release"));
   assert(applied == 4);
   fail_release = true;
   assert(rr_ptt_set_all_off_reason("failed-safety-stop"));
   fail_release = false;
   assert(!rr_ptt_set_all_off_reason("retry-safety-stop"));
   assert(applied == 6);
   dict_free(cfg);
   cfg = NULL;
   use_worker = true;
   unsigned before = applied;
   global_tot_time = 789;
   assert(!rr_ptt_request(VFO_A, false, "queued-release"));
   assert(applied == before && global_tot_time == 789);
   void *first_release = queued_user;
   assert(!rr_ptt_request(VFO_A, false, "duplicate-queued-release"));
   assert(queued_user == first_release);
   queued_completion(&fixture_backend, (rr_server_vfo_t *) 1, false, queued_user);
   assert(applied == before && global_tot_time == 0);
   global_tot_time = 456;
   assert(!rr_ptt_request(VFO_A, false, "failed-queued-release"));
   queued_completion(&fixture_backend, (rr_server_vfo_t *) 1, true, queued_user);
   assert(global_tot_time == 456 && rr_ptt_check_blocked());
   assert(!rr_ptt_request(VFO_A, false, "retry-queued-release"));
   queued_completion(&fixture_backend, (rr_server_vfo_t *) 1, false, queued_user);
   assert(global_tot_time == 0);
   puts("PASS: asynchronous PTT release updates confirmed state only after completion and retains safety lockout on failure");
   puts("PASS: TX lockout blocks key-down but permits safety key-up");
}

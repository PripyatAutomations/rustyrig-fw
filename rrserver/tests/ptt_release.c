// Safety lockouts must inhibit key-down while always allowing key-up.
#include <assert.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <rrserver/ptt.h>
#include <rrserver/globalstate.h>
#include <rrserver/backend.h>
#include <rrserver/database.h>
#include <rrserver/media.h>
#include <rrserver/au.h>
#include <rrserver/rig.registry.h>
struct GlobalState rig;
time_t now, ptt_tot_time;
bool dying, restarting;
sqlite3 *masterdb;
static unsigned applied;
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
int db_ptt_start(sqlite3 *db, const char *user, const char *vfo, double frequency, const char *mode, int width, float power, const char *file, const char *id) {
   assert(false);

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
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: TX lockout blocks key-down but permits safety key-up");
}

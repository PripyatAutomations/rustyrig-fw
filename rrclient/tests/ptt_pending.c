#include <assert.h>
#include "rrclient/gtk/gtk.ptt-btn.c"
bool dying, restarting, ptt_active;
time_t now;
const char *login_user = "operator";
struct rr_user *global_userlist;
char vfo_state_get_active(void) { return 'A'; }
bool vfo_state_get_bool(const char *vfo, const char *key, bool fallback) { return false; }
const char *rrclient_current_room(void) { return "#rig"; }
void vfo_controls_set_ptt_state(bool locked, bool local) {}
int main(void) {
   ptt_button_pending = true;
   ptt_button_pending_state = true;
   ptt_button_pending_expire = 102;
   now = 100;
   ptt_button_set_state(false);
   assert(ptt_button_pending); // stale contradictory echo
   ptt_button_set_state(true);
   assert(!ptt_button_pending && ptt_active && !ptt_button_pending_expire);
   ptt_button_pending = true;
   ptt_button_pending_state = false;
   ptt_button_pending_quiet = true;
   ptt_button_pending_expire = 102;
   ptt_button_set_state(true);
   assert(ptt_button_pending && ptt_button_pending_quiet);
   ptt_button_set_state(false);
   assert(!ptt_button_pending && !ptt_active && !ptt_button_pending_quiet);
   ptt_button_pending = true;
   ptt_button_pending_expire = 102;
   ptt_button_refresh();
   assert(ptt_button_pending);
   now = 102;
   ptt_button_refresh();
   assert(!ptt_button_pending && !ptt_button_pending_expire && !ptt_active);
   puts("PASS: GTK matching PTT acknowledgements, stale echoes, and timeout");
}

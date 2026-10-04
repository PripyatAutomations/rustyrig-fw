#include <assert.h>
#include "rrclient/events.c"
#include "rrclient/frontend.c"

bool dying, restarting;
time_t now;
const char *login_user = "operator";
static unsigned confirmations;
static bool confirmed;
static dict *observed;
const char *rrclient_media_active_room(void) { return "#site-rig0"; }
const struct rr_client_media_chan *rrclient_media_current_channel(bool tx) { return NULL; }
char vfo_state_get_active(void) { return 'A'; }
bool vfo_set_dict(const char *vfo, dict *d) {
   assert(!vfo);
   if (dict_get_type(d, "cat.state.ptt") != VAL_END) {
      dict_add_bool(observed, "ptt", dict_get_bool(d, "cat.state.ptt", false));
      dict_add(observed, "vfo", dict_get(d, "cat.state.vfo", "A"));
   }
   return false;
}
bool userlist_add_or_update(dict *d) { return true; }
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {}
static void acknowledge(bool active) { confirmations++; confirmed = active; }
int main(void) {
   rr_frontend_ops_t ops = { .name = "test", .ptt_set_state = acknowledge };
   registered_ops = &ops;
   observed = dict_new();
   rrclient_handle_cat(NULL, "{\"cat\":{\"cmd\":\"ptt\",\"user\":\"OPERATOR\",\"vfo\":\"A\",\"ptt\":true}}", NULL, NULL);
   assert(confirmations == 1 && confirmed);
   assert(dict_get_bool(observed, "ptt", false));
   assert(!strcmp(dict_get(observed, "vfo", ""), "A"));
   rrclient_handle_cat(NULL, "{\"cat\":{\"cmd\":\"ptt\",\"user\":\"other\",\"vfo\":\"A\",\"ptt\":true}}", NULL, NULL);
   rrclient_handle_cat(NULL, "{\"cat\":{\"cmd\":\"ptt\",\"user\":\"operator\",\"vfo\":\"B\",\"ptt\":true}}", NULL, NULL);
   rrclient_handle_cat(NULL, "{\"cat\":{\"state\":{\"vfo\":\"A\",\"freq\":14075000}}}", NULL, NULL);
   assert(confirmations == 1);
   rrclient_handle_userinfo(NULL, "{\"talk\":{\"user\":\"operator\",\"tx\":true,\"ptt-vfo\":\"A\"}}", NULL, NULL);
   assert(confirmations == 2 && confirmed);
   rrclient_handle_userinfo(NULL, "{\"talk\":{\"user\":\"other\",\"tx\":false}}", NULL, NULL);
   assert(confirmations == 2);
   rrclient_handle_userinfo(NULL, "{\"talk\":{\"user\":\"operator\",\"tx\":false}}", NULL, NULL);
   assert(confirmations == 3 && !confirmed);
   dict_free(observed);
   registered_ops = NULL;
   puts("PASS: native CAT and userinfo PTT confirmations, ownership, and VFO isolation");
}

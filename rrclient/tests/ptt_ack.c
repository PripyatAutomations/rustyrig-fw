#include <assert.h>
#include "rrclient/events.c"
#include "rrclient/frontend.c"

bool dying, restarting;
time_t now;
const char *login_user = NULL;
rrconn_t *ws_conn;
enum GuiMode ui_mode;
static unsigned confirmations;
static unsigned room_list_requests;
static unsigned room_rejoin_calls;
static char listed_rooms[128];
static bool confirmed;
static dict *observed;
bool ui_print(const char *window, const char *fmt, ...) { return true; }
bool cmd_list(int argc, char **args) {
   assert(argc == 0 && args == NULL);
   room_list_requests++;
   return false;
}
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
void userlist_clear_all(void) {}
void rrclient_rooms_clear(void) {}
void rrclient_rooms_disconnect(void) {}
void rrclient_rooms_set_available(const char *rooms) {
   snprintf(listed_rooms, sizeof(listed_rooms), "%s", rooms ? rooms : "");
}
void rrclient_rooms_rejoin_available(void) { room_rejoin_calls++; }
const char *ui_active_window_name(void) { return "status"; }
void Log(logpriority_t priority, const char *subsys, const char *fmt, ...) {}
static void acknowledge(bool active) { confirmations++; confirmed = active; }
static void connection_update(int state) { (void)state; }
static void ptt_online(bool online) { (void)online; }
int main(void) {
   rr_frontend_ops_t ops = { .name = "test", .ptt_set_state = acknowledge,
      .conn_button_update = connection_update, .ptt_set_online = ptt_online };
   registered_ops = &ops;
   login_user = strdup("operator");
   observed = dict_new();
   rrclient_handle_connection("connected", "{\"auth\":{\"user\":\"operator\"}}", NULL, NULL);
   assert(room_list_requests == 0);
   rrclient_handle_connection("authorized", NULL, NULL, NULL);
   assert(room_list_requests == 1);
   rrclient_handle_room_list(NULL,
      "{\"talk\":{\"rooms\":\"#rig #channel\"}}", NULL, NULL);
   assert(!strcmp(listed_rooms, "#rig #channel") && room_rejoin_calls == 1);
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
   puts("PASS: room-list request after auth and native PTT confirmation/ownership handling");
}

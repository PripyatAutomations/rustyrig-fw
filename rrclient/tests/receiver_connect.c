#include <assert.h>
#include <rrclient/receiver.connect.c>
time_t now;
static rrconn_t alpha = {
   .is_ws = true, .authenticated = true
}, beta;

static receiver_directory_entry_t receiver = {
   .driver = "kiwi", .url = "http://receiver.invalid:8073/", .ext_api = 2
};

static dict *sent;
static unsigned joins;
const receiver_directory_entry_t *receiver_directory_find_url(const char *url) {
   return !strcmp(receiver.url, url) ? &receiver : NULL;
}
size_t receiver_directory_count(void) {
   return 1;
}
const receiver_directory_entry_t *receiver_directory_receiver(size_t index) {
   assert(!index);

   return &receiver;
}
bool receiver_directory_entry_accessible(const receiver_directory_entry_t *r) {
   return r && r->ext_api > 0;
}
const char *rrclient_selected_server(void) {
   return "alpha";
}
rrconn_t *rrclient_active_connection(void) {
   return &alpha;
}
const char *rrclient_connection_name(const rrconn_t *connection) {
   assert(connection == &alpha);

   return "alpha";
}
rrconn_t *rrclient_connection_find(const char *profile) {
   assert(!strcmp(profile, "alpha"));

   return &alpha;
}
void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   assert(connection == &alpha);
   callback(data);
}
bool ui_print(const char *room, const char *fmt, ...) {
   return true;
}
bool ws_send_dict(rrconn_t *from, rrconn_t *to, dict *message, int opcode) {
   assert(to == &alpha);
   dict_free(sent);
   sent = dict_new();
   dict_merge(sent, message);

   return true;
}
bool cmd_join(int argc, char **args) {
   assert(argc == 2 && !strcmp(args[1], "#test-rig1"));
   joins++;

   return false;
}
static void reply(rrconn_t *connection, const char *request, const char *error) {
   dict *result = dict_new(), *message = dict_new();
   dict_add(result, "request", request);
   dict_add(result, "room", "#test-rig1");
   dict_add(result, "error", error);
   char *json = dict2json(result);
   dict_add(message, "talk.action", "websdr-result");
   dict_add(message, "talk.data", json);
   event_emit_dict("admin.result", connection, message);
   free(json);
   dict_free(message);
   dict_free(result);
}
int main(void) {
   event_init();
   rrclient_receiver_connect_init();
   assert(rrclient_receiver_connect(receiver.url, false));
   assert(!rrclient_receiver_connect(receiver.url, false));
   char *request = g_strdup(pending.request);
   reply(&beta, request, "");
   reply(&alpha, "stale", "");
   assert(!joins);
   reply(&alpha, request, "");
   assert(joins == 1 && !pending.request);
   g_free(request);
   assert(rrclient_receiver_connect(receiver.url, true));
   reply(&alpha, pending.request, "");
   assert(joins == 1 && !pending.request);
   assert(rrclient_receiver_connect(receiver.url, false));
   request = g_strdup(pending.request);
   event_emit("disconnected", &alpha, NULL);
   reply(&alpha, request, "");
   assert(joins == 1);
   g_free(request);
   assert(rrclient_receiver_connect(receiver.url, false));
   reply(&alpha, pending.request, "websdr.connect required");
   assert(joins == 1);
   assert(rrclient_receiver_connect("http://manual.invalid:8073/", false));
   reply(&alpha, pending.request, "");
   assert(joins == 2);
   receiver.ext_api = 0;
   assert(!rrclient_receiver_connect(receiver.url, false));
   rrclient_receiver_connect_shutdown();
   event_shutdown();
   dict_free(sent);
   puts("PASS: receiver actions bind server/request, join only on success, ignore stale replies and honor API policy");
}

#include <assert.h>
#include <rrclient/ui.c>
rrconn_t *ws_conn;
static rrconn_t first, second;
const rr_frontend_ops_t *frontend_ops(void) {
   return NULL;
}
bool frontend_present(void) {
   return false;
}
void rrclient_connection_select_window(const char *window) {
   assert(false);
}
const char *rrclient_window_room(const char *window) {
   return window;
}
rrconn_t *rrclient_connection_for_window(const char *window) {
   return NULL;
}
bool rrclient_window_is_shared(const char *window) {
   return false;
}
bool rrclient_window_is_status(const char *window) {
   return false;
}
void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   rrconn_t *old = ws_conn;
   ws_conn = connection;
   callback(data);
   ws_conn = old;
}
static void inner(void *data) {
   assert(rrclient_active_connection() == &second && !strcmp(rrclient_active_room(), "#inner"));
}
static void outer(void *data) {
   assert(rrclient_active_connection() == &first && !strcmp(ui_active_window_name(), "#outer"));
   rrclient_input_dispatch(&second, "#inner", inner, NULL);
   assert(rrclient_active_connection() == &first && !strcmp(rrclient_active_room(), "#outer"));
}
int main(void) {
   ui_mode = UI_MODE_NONE;
   rrclient_input_dispatch(&first, "#outer", outer, NULL);
   assert(!input_context && !ws_conn && !ui_active_window_name());
   puts("PASS: nested asynchronous input contexts retain server/room and restore ordinary UI state");
}

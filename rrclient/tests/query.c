#include <assert.h>
#include <stdarg.h>
#include <rrclient/cmd.chat.c>
bool dying;
time_t now;
enum GuiMode ui_mode = UI_MODE_GTK;
static rrconn_t connection = {
   .authenticated = true
};

rrconn_t *ws_conn = &connection;
static char query_target[128], sent_target[128], sent_text[1024];
static unsigned sends;
const char *rrclient_window_name(const char *name) {
   return name;
}
rrconn_t *rrclient_active_connection(void) {
   return &connection;
}
const char *ui_active_window_name(void) {
   return "#origin";
}
static void query(const char *name) {
   snprintf(query_target, sizeof(query_target), "%s", name);
}

static const rr_frontend_ops_t ops = {
   .chat_query_add = query
};
const rr_frontend_ops_t *frontend_ops(void) {
   return &ops;
}
bool ui_print(const char *window, const char *format, ...) {
   return false;
}
static bool capture(rrconn_t *dest, dict *message) {
   assert(dest == &connection);
   assert(!strcmp(dict_get(message, "talk.msg_type", ""), "priv"));
   snprintf(sent_target, sizeof(sent_target), "%s", dict_get(message, "talk.target", ""));
   snprintf(sent_text, sizeof(sent_text), "%s", dict_get(message, "talk.data", ""));
   sends++;

   return true;
}
bool irc_send_dict(rrconn_t *dest, dict *message) {
   return capture(dest, message);
}
bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *message, int opcode) {
   return capture(dest, message);
}
int main(void) {
   char *args[] = {
      "query", "alice", "hello with  spaces and /slashes"
   };

   for (int ws = 0 ; ws < 2 ; ws++) {
      connection.is_ws = ws;
      unsigned before = sends;
      assert(!cmd_query(2, args) && sends == before);
      assert(!strcmp(query_target, "alice"));
      assert(!cmd_query(3, args) && sends == before + 1);
      assert(!strcmp(sent_target, "alice"));
      assert(!strcmp(sent_text, args[2]));
   }

   args[1] = "alice impossible";
   assert(cmd_query(2, args));
   puts("PASS: query opens one user and sends optional trailing text via IRC and RustyRig");
}

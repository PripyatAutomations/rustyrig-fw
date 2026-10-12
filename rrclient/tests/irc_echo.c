#include <assert.h>
#include <stdarg.h>
#include "rrclient/events.c"
#include "rrclient/m_privmsg.c"

bool dying, restarting;
time_t now = 100;
const char *login_user = "tester";
rrconn_t *ws_conn;
enum GuiMode ui_mode = UI_MODE_GTK;
static unsigned printed;
static char output[512], destination[128], wire[512];
static bool send_ok = true;
const rr_frontend_ops_t *frontend_ops(void) {
   return NULL;
}
const char *ui_active_window_name(void) {
   return "status";
}
const char *rrclient_window_name(const char *room) {
   return room;
}
const char *rrclient_window_room(const char *window) {
   return window;
}
rrconn_t *rrclient_connection_for_window(const char *window) {
   (void)window;

   return ws_conn;
}
void ui_message_bell(void) {
   assert(false);
}
bool ui_print(const char *window, const char *fmt, ...) {
   snprintf(destination, sizeof(destination), "%s", window ? window : "status");
   va_list args;
   va_start(args, fmt);
   vsnprintf(output, sizeof(output), fmt, args);
   va_end(args);
   printed++;

   return false;
}
bool irc_send(rrconn_t *connection, const char *fmt, ...) {
   assert(connection == ws_conn);
   va_list args;
   va_start(args, fmt);
   vsnprintf(wire, sizeof(wire), fmt, args);
   va_end(args);

   return send_ok;
}
int main(void) {
   rrconn_t connection = {
      .fd = -1
   };
   snprintf(connection.nick, sizeof(connection.nick), "%s", login_user);
   ws_conn = &connection;
   event_init();
   event_on("irc.sent", rrclient_irc_send_event, NULL);
   event_on("talk.msg", rrclient_handle_talk_msg, NULL);
   dict *message = json2dict("{\"talk\":{\"cmd\":\"msg\",\"msg_type\":\"pub\",\"data\":\"hello\",\"target\":\"#hamradio\"},\"msg\":{\"type\":\"talk\"}}");
   assert(message);
   assert(irc_send_dict(&connection, message));
   assert(printed == 1 && !strcmp(destination, "#hamradio"));
   assert(strstr(output, "=>") && strstr(output, "hello"));
   assert(!strcmp(wire, "PRIVMSG #hamradio :hello"));
   send_ok = false;
   assert(!irc_send_dict(&connection, message));
   assert(printed == 1);
   /* Sender-less input must also be safe at the common rendering boundary. */
   event_emit_dict("talk.msg", &connection, message);
   assert(printed == 2 && strstr(output, "?"));
   dict_free(message);
   event_shutdown();
   puts("PASS: IRC room sends echo once through normalized chat events; failed sends and missing senders are safe");

   return 0;
}

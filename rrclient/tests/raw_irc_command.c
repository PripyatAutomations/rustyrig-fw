// Raw IRC command forwarding and /quote alias callback behavior.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rrclient/cmd.admin.c"

bool dying, restarting;
rrconn_t *ws_conn;
bool ws_send_dict(rrconn_t *source, rrconn_t *client, dict *data, int opcode) {
   assert(false);
   return false;
}
static rrconn_t connection;
static char sent[IRC_MSGLEN + 1];
static bool send_ok = true;

rrconn_t *rrclient_active_connection(void) {
   return &connection;
}

const char *ui_active_window_name(void) {
   return "#room";
}

const char *rrclient_active_room(void) {
   return "#room";
}

bool ui_print(const char *window, const char *fmt, ...) {
   (void)window;
   (void)fmt;
   return false;
}

bool irc_send(rrconn_t *cptr, const char *fmt, ...) {
   assert(cptr == &connection);
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(sent, sizeof(sent), fmt, ap);
   va_end(ap);
   return send_ok && !strchr(sent, '\r') && !strchr(sent, '\n');
}

int main(void) {
   char *quote[] = {"quote", "WHOIS example :some user"};
   assert(!connection.is_ws);
   assert(!cmd_raw(2, quote));
   assert(!strcmp(sent, "WHOIS example :some user"));

   char *raw[] = {"raw", "FOOBAR", "#room", "argument"};
   assert(!cmd_raw(4, raw));
   assert(!strcmp(sent, "FOOBAR #room argument"));

   char *newline[] = {"raw", "PRIVMSG #room :line1\nline2"};
   assert(cmd_raw(2, newline));

   connection.connected = 1;
   connection.authenticated = true;
   char *kick[] = { "kick", "alice", "Please follow channel rules" };
   assert(!cmd_kick(3, kick));
   assert(!strcmp(sent, "KICK #room alice :Please follow channel rules"));
   assert(!cmd_kick(2, kick));
   assert(!strcmp(sent, "KICK #room alice"));
   assert(!rrclient_kick_send(&connection, "#menu-room", "menu-window", 3, kick));
   assert(!strcmp(sent, "KICK #menu-room alice :Please follow channel rules"));
   char *bad_kick[] = { "kick", "alice bob" };
   sent[0] = 0;
   cmd_kick(2, bad_kick);
   assert(!sent[0]);
   send_ok = false;
   assert(cmd_kick(3, kick));
   send_ok = true;
   char *ban[] = { "mode", "+b", "*!*@bad.example" };
   assert(!cmd_mode(3, ban));
   assert(!strcmp(sent, "MODE #room +b *!*@bad.example"));
   char *op[] = { "mode", "+o", "alice" };
   assert(!cmd_mode(3, op));
   assert(!strcmp(sent, "MODE #room +o alice"));
   char *voice[] = { "mode", "-v", "alice" };
   assert(!cmd_mode(3, voice));
   assert(!strcmp(sent, "MODE #room -v alice"));
   char *explicit_room[] = { "mode", "#other", "-o", "bob" };
   assert(!cmd_mode(4, explicit_room));
   assert(!strcmp(sent, "MODE #other -o bob"));
   send_ok = false;
   assert(cmd_raw(4, raw));

   puts("PASS: raw/quote preserve arbitrary IRC commands and report send failures");
   return 0;
}

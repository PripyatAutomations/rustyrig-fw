// Raw IRC command forwarding and /quote alias callback behavior.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "rrclient/cmd.admin.c"

bool dying, restarting;
static rrconn_t connection;
static char sent[IRC_MSGLEN + 1];
static bool send_ok = true;

rrconn_t *rrclient_active_connection(void) {
   return &connection;
}

const char *ui_active_window_name(void) {
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

   send_ok = false;
   assert(cmd_raw(4, raw));

   puts("PASS: raw/quote preserve arbitrary IRC commands and report send failures");
   return 0;
}

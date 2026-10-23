#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now = 100;
bool dying, restarting;
static unsigned sends, echoes, fail_at;
static size_t longest;
static char combined[8192], echoed[8192];
static rrconn_t *expected;
static const char *kind;

bool irc_send(rrconn_t *connection, const char *format, ...) {
   assert(connection == expected);
   char line[IRC_MSGLEN + 1];
   va_list args;
   va_start(args, format);
   int n = vsnprintf(line, sizeof(line), format, args);
   va_end(args);
   assert(n >= 0 && (size_t)n + 2 <= irc_line_limit(connection));
   assert(!strchr(line, '\r') && !strchr(line, '\n'));
   sends++;

   if (fail_at && sends == fail_at) {
      return false;
   }

   if ((size_t)n + 2 > longest) {
      longest = (size_t)n + 2;
   }
   char *text = strstr(line, " :");
   assert(text);
   text += 2;

   if (!strcmp(kind, "action")) {
      assert(!strncmp(text, "\001ACTION ", 8));
      assert(text[strlen(text) - 1] == '\001');
      text[strlen(text) - 1] = '\0';
      text += 8;
   }
   /* These test inputs are valid UTF-8; chunks must not begin mid-character. */
   assert(!*text || ((unsigned char)*text & 0xc0) != 0x80);
   assert(strlcat(combined, text, sizeof(combined)) < sizeof(combined));

   return true;
}
static void observe(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)user;
   assert(connection == expected);
   dict *d = json2dict(data);
   assert(d && !strcmp(dict_get(d, "talk.msg_type", ""), kind));
   assert(strlcat(echoed, dict_get(d, "talk.data", ""), sizeof(echoed)) < sizeof(echoed));
   echoes++;
   dict_free(d);
}
static dict *message(const char *text, const char *type) {
   dict *d = dict_new();
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.target", "#room");
   dict_add(d, "talk.msg_type", type);
   dict_add(d, "talk.data", text);

   return d;
}
static void reset(const char *type) {
   kind = type;
   sends = echoes = fail_at = 0;
   longest = 0;
   combined[0] = echoed[0] = '\0';
}
static void check(const char *text, const char *type, unsigned minimum) {
   reset(type);
   dict *d = message(text, type);
   assert(irc_send_dict(expected, d));
   assert(sends >= minimum && sends == echoes);
   assert(!strcmp(combined, text) && !strcmp(echoed, text));
   assert(!strcmp(dict_get(d, "talk.data", ""), text));
   dict_free(d);
}
int main(void) {
   rrconn_t connection = {
      .fd = -1
   };
   expected = &connection;
   event_init();
   assert(!irc_init());
   event_on("irc.sent", observe, NULL);
   char text[4097];
   memset(text, 'a', sizeof(text) - 1);
   text[sizeof(text) - 1] = '\0';
   check(text, "pub", 9);
   assert(longest == 512);
   check("", "pub", 1);
   irc_process_message(expected, ":server 005 nick LINELEN=80 :supported");
   check("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen", "pub", 2);
   check(text, "action", 2);
   check(text, "notice", 2);
   char unicode[601];

   for (unsigned i = 0 ; i < 200 ; i++) {
      memcpy(unicode + i * 3, "\xe2\x82\xac", 3);
   }

   unicode[600] = '\0';
   check(unicode, "pub", 2);
   reset("pub");
   dict *d = message(text, "pub");
   fail_at = 2;
   assert(!irc_send_dict(expected, d) && sends == 2 && echoes == 1);
   assert(!strcmp(combined, echoed));
   dict_free(d);
   reset("pub");
   d = message("safe\nPRIVMSG #room :injected", "pub");
   assert(!irc_send_dict(expected, d) && sends == 0 && echoes == 0);
   dict_free(d);
   /* A server's larger limit is still bounded by our transport capacity. */
   irc_process_message(expected, ":server 005 nick LINELEN=2048 :supported");
   check(text, "pub", 5);
   assert(longest == IRC_MSGLEN);
   irc_shutdown();
   event_shutdown();
   puts("PASS: IRC advertised/default limits, lossless word/UTF-8 splitting, actions/notices, echoes and partial failures");

   return 0;
}

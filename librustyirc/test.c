#include "irc.h"
#include "connection.h"
#include <assert.h>
#include <string.h>

static unsigned messages, audio_offers, sent;
static char last_wire[512];
static bool on_send(void *unused, const char *data, size_t n) {
   (void)unused;
   assert(n < sizeof(last_wire));
   memcpy(last_wire, data, n);
   last_wire[n] = 0;
   ++sent;
   return true;
}
static void on_message(void *unused, rustyirc_connection_t *connection,
                       const rustyirc_message_t *message) {
   (void)unused; (void)connection;
   assert(message && message->argc);
   ++messages;
}
static void on_audio(void *unused, rustyirc_connection_t *connection,
                     const rustyirc_message_t *message, const char *payload) {
   (void)unused; (void)connection;
   assert(message && payload && strstr(payload, "DCC AUDIO"));
   ++audio_offers;
}
static void test_connection(void) {
   rustyirc_callbacks_t cb = { .send = on_send, .on_message = on_message,
                              .on_dcc_audio = on_audio };
   rustyirc_connection_t *c = rustyirc_connection_new(&cb);
   assert(c && rustyirc_connection_set_nick(c, "tester"));
   assert(rustyirc_connection_send(c, "PING :123"));
   assert(sent == 1 && !strcmp(last_wire, "PING :123\r\n"));
   assert(!rustyirc_connection_registered(c));
   const char welcome[] = ":server 001 tester :Welcome\r";
   assert(rustyirc_connection_feed(c, welcome, strlen(welcome)));
   assert(!rustyirc_connection_registered(c));
   assert(rustyirc_connection_feed(c, "\n", 1));
   assert(rustyirc_connection_registered(c));
   const char offer[] = ":alice!u@h PRIVMSG tester :\001DCC AUDIO 1 payload\001\r\n";
   assert(rustyirc_connection_feed(c, offer, strlen(offer)));
   assert(audio_offers == 1 && messages == 1);
   rustyirc_connection_reset(c);
   assert(!rustyirc_connection_registered(c));
   rustyirc_connection_free(c);
}

int main(void) {
   test_connection();
   rustyirc_message_t *m = rustyirc_parse(":irc.example 311 dan mike ~mi localhost * :Mike's such a cool person!");
   assert(m);
   const char *nick, *user, *host, *realname;
   assert(rustyirc_whoisuser(m, &nick, &user, &host, &realname));
   assert(!strcmp(nick, "mike") && !strcmp(user, "~mi"));
   assert(!strcmp(host, "localhost") && !strcmp(realname, "Mike's such a cool person!"));
   rustyirc_message_free(m);
   m = rustyirc_parse(":server 311 tester bob user 2001:db8::1 * :Unknown");
   assert(m && rustyirc_whoisuser(m, &nick, &user, &host, &realname));
   assert(!strcmp(host, "2001:db8::1"));
   rustyirc_message_free(m);
   m = rustyirc_parse(":server 311 me nick user host invalid :Name");
   assert(m && !rustyirc_whoisuser(m, NULL, NULL, NULL, NULL));
   rustyirc_message_free(m);
   assert(!rustyirc_parse("PING :line\r\n"));
   return 0;
}

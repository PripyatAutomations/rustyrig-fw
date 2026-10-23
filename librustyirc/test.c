#include "irc.h"
#include <assert.h>
#include <string.h>
int main(void) {
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

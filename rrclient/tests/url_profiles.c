// Shared URL/profile regression: runs under native and Mongoose profiles.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librrprotocol/server.url.h>
#include <rrclient/connman.h>

int main(void) {
   rr_server_url_t url;
   assert(rr_server_url_parse("wss://radio.example:4420/ws", &url));
   assert(url.transport == RR_TRANSPORT_WSS && url.tls && !url.irc && url.port == 4420);
   assert(rr_server_url_parse("ircs://[::1]", &url));
   assert(url.transport == RR_TRANSPORT_IRCS && url.tls && url.irc && url.ipv6 && url.port == 6697);
   assert(rr_server_url_parse("ws://localhost/ws/", &url) && url.port == 8420);
   const char *invalid[] = {"http://localhost", "wss://:4420/ws", "wss://localhost:0/ws",
                             "wss://localhost:65536/ws", "wss://localhost/ws/#frag", NULL};
   for (unsigned i = 0; invalid[i]; ++i)
      assert(!rr_server_url_parse(invalid[i], &url));
   char profile[64];
   assert(rrclient_server_profile_name("server:rplywv00.server.url", profile, sizeof(profile)));
   assert(!strcmp(profile, "rplywv00"));
   assert(!rrclient_server_profile_name("server:rplywv00.server.pass", profile, sizeof(profile)));
   assert(!rrclient_server_profile_name("server:rplywv00.server.url", profile, 4));
   puts("PASS: shared client URL schemes, IPv6, port validation and profile-key parsing");
}

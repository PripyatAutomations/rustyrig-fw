#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now;
bool dying, restarting;
const char *server_name = "loopback";
extern struct mg_str tls_ca_path_str;
static bool complete;
static void observe(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)cptr;
   (void)user;
   dict *d = json2dict(data);

   if (d && !strcmp(dict_get(d, "msg.cmd", ""), "NOTICE") && !strcmp(dict_get(d, "msg.arg2", ""), "probe-complete")) {
      complete = true;
   }
   dict_free(d);
}
int main(int argc, char **argv) {
   assert(argc == 2);
   now = time(NULL);
   server_cfg_t server = {
      0
   };
   snprintf(server.host, sizeof(server.host), "127.0.0.1");
   snprintf(server.pass, sizeof(server.pass), "secret");
   rrconn_t client = {
      .fd = -1, .server = &server
   };
   snprintf(client.nick, sizeof(client.nick), "tester");
   struct mg_mgr manager;
   mg_mgr_init(&manager);
   mg_log_set(MG_LL_ERROR);
   /* Self-signed loopback fixture; production uses the existing WS CA setting. */
   tls_ca_path_str = mg_str("*");
   event_init();
   event_on("irc.message", observe, NULL);
   assert(!irc_init());
   assert(mg_connect(&manager, argv[1], irc_mongoose_handler, &client));

   for (unsigned i = 0 ; i < 500 && !complete ; i++) {
      now = time(NULL);
      mg_mgr_poll(&manager, 10);
   }

   mg_mgr_free(&manager);
   irc_shutdown();
   event_shutdown();
   assert(complete);
   puts("PASS: live IRC registration and PING/PONG through local TCP/TLS transport");

   return 0;
}

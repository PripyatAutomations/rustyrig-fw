#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>

time_t now;
bool dying, restarting;
const char *server_name, *login_user;
rrconn_t *ws_conn, *ws_tx_conn;
struct mg_mgr mgr;
char session_token[HTTP_TOKEN_LEN + 1];
extern struct mg_str tls_ca_path_str;
extern char *tls_ca_path;
static unsigned ready;
bool ui_print(const char *window, const char *format, ...) {
   (void)window;
   va_list ap;
   va_start(ap, format);
   vfprintf(stderr, format, ap);
   fputc('\n', stderr);
   va_end(ap);

   return true;
}
void rrclient_update_connection_ui(int connected) {
   ws_connected = connected;
}
void tui_refresh_sb_online(void) {
}
void show_server_chooser(void) {
}
static void observe(const char *event, const char *data, rrconn_t *c, void *user) {
   (void)data;
   (void)user;

   if (!strcmp(event, "irc.connected") || (c && c->conn && c->conn->is_websocket)) {
      ready++;
   }
}
int main(int argc, char **argv) {
   assert(argc == 2);
   now = time(NULL);
   cfg = dict_new();
   event_init();
   connman_register_events();
   event_on("irc.connected", observe, NULL);
   event_on("connected", observe, NULL);
   mg_mgr_init(&mgr);
   mg_log_set(MG_LL_ERROR);
   tls_ca_path = "*";
   tls_ca_path_str = mg_str("*");
   const char *urls[] = {
      "irc://target.invalid", "ircs://target.invalid", "ws://target.invalid/path?test=1", "wss://target.invalid/path?test=1"
   };

   for (unsigned i = 0 ; i < 4 ; i++) {
      char name[32], key[128];
      snprintf(name, sizeof(name), "proxy%u", i);
      snprintf(key, sizeof(key), "server:%s.server.proxy", name);
      dict_add(cfg, key, argv[1]);

      if (i >= 2) {
         snprintf(key, sizeof(key), "server:%s.server.proxy.user", name);
         dict_add(cfg, key, "user");
         snprintf(key, sizeof(key), "server:%s.server.proxy.pass", name);
         dict_add(cfg, key, "secret");
      }
      assert(!rrclient_connect_url(name, urls[i]));
   }

   for (unsigned i = 0 ; i < 1000 && ready < 4 ; i++) {
      now = time(NULL);
      mg_mgr_poll(&mgr, 10);
   }

   dying = true;
   rrclient_quit_servers("fixture shutdown");
   rrclient_flush_quit();
   mg_mgr_free(&mgr);
   connman_shutdown();
   irc_shutdown();
   event_shutdown();
   dict_free(cfg);
   assert(ready == 4);
   puts("PASS: concurrent IRC/IRCS/WS/WSS tunnels with proxy DNS, auth and deferred TLS/upgrade");
}

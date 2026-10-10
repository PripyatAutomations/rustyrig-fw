#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>

time_t now = 100, poll_block_expire, poll_block_delay;
bool dying, restarting;
const char *server_name;
const char *login_user;
rrconn_t *ws_conn, *ws_tx_conn;
struct mg_mgr mgr;
char session_token[HTTP_TOKEN_LEN + 1];
static struct mg_connection connections[12];
static unsigned connection_count, ws_calls, tcp_calls;
static char last_url[2048];
static struct mg_connection *fake_connect(const char *url, mg_event_handler_t fn, void *data) {
   assert(connection_count < 12);
   struct mg_connection *c = &connections[connection_count++];
   snprintf(last_url, sizeof(last_url), "%s", url);
   c->fn = fn;
   c->fn_data = data;
   c->is_tls = mg_url_is_ssl(url);
   fn(c, MG_EV_OPEN, NULL);
   return c;
}
struct mg_connection *mg_connect(struct mg_mgr *manager, const char *url, mg_event_handler_t fn, void *data) {
   (void)manager;
   tcp_calls++;
   return fake_connect(url, fn, data);
}
struct mg_connection *mg_ws_connect(struct mg_mgr *manager, const char *url, mg_event_handler_t fn, void *data, const char *fmt, ...) {
   (void)manager;
   (void)fmt;
   ws_calls++;
   return fake_connect(url, fn, data);
}
bool ui_print(const char *window, const char *fmt, ...) {
   (void)window;
   (void)fmt;
   return false;
}
void rrclient_update_connection_ui(int connected) { (void)connected; }
void tui_refresh_sb_online(void) {}
void userlist_clear_all(void) {}
void show_server_chooser(void) {}
static void release_active(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)event;
   (void)data;
   (void)conn;
   (void)user;
   free(ws_conn);
   ws_conn = NULL;
}
int main(void) {
   event_init();
   event_on("disconnected", release_active, NULL);
   event_on("http.error", release_active, NULL);
   cfg = dict_new();
   dict_add(cfg, "server:test.server.user", "tester");
   dict_add(cfg, "server:test.server.pass", "secret");
   assert(rrclient_connect_url("test", "localhost:6667"));
   assert(!connection_count);
   assert(!rrclient_connect_url("test", "irc://localhost:6667"));
   assert(tcp_calls == 1 && !ws_calls && !strcmp(last_url, "tcp://localhost:6667"));
   assert(ws_conn->server && !ws_conn->is_ws && !strcmp(ws_conn->nick, "tester"));
   struct mg_connection *stale = ws_conn->conn;
   assert(!disconnect_server(server_name));
   assert(stale->is_closing);
   assert(!rrclient_connect_url("test", "ircs://[::1]:6697"));
   assert(tcp_calls == 2 && !strcmp(last_url, "tls://[::1]:6697"));
   assert(ws_conn->server->tls && ws_conn->conn->is_tls);
   rrconn_t *active = ws_conn;
   stale->fn(stale, MG_EV_CLOSE, NULL);
   assert(ws_conn == active && active->conn);
   assert(!disconnect_server(server_name));
   active->conn->fn(active->conn, MG_EV_CLOSE, NULL);
   /* Standalone probe has no native IRC event adapter: release it here. */
   free(ws_conn);
   ws_conn = NULL;
   assert(!rrclient_connect_url("test", "WS://localhost:8420/ws/"));
   assert(ws_calls == 1 && !strcmp(last_url, "ws://localhost:8420/ws/"));
   assert(ws_conn->is_ws && !ws_conn->server);
   struct mg_connection *failed = ws_conn->conn;
   failed->fn(failed, MG_EV_ERROR, "test failure");
   assert(!ws_conn && !failed->fn_data);
   failed->fn(failed, MG_EV_CLOSE, NULL);
   assert(!rrclient_connect_url("test", "wss://example.test:4420/ws/"));
   assert(ws_calls == 2 && ws_conn->conn->is_tls);
   assert(!strcmp(last_url, "wss://example.test:4420/ws/"));
   struct mg_connection *last = ws_conn->conn;
   assert(!disconnect_server(server_name));
   last->fn(last, MG_EV_CLOSE, NULL);
   assert(!ws_conn);
   free((void *)server_name);
   dict_free(cfg);
   cfg = NULL;
   event_shutdown();
   puts("PASS: native URL transport selection, TLS flags, stale closes and error teardown");
   return 0;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/rooms.h>
#include <rrclient/frontend.h>
#include <rrclient/cmd.h>

time_t now = 100, poll_block_expire, poll_block_delay;
bool dying, restarting;
const char *server_name, *login_user;
rrconn_t *ws_conn, *ws_tx_conn;
struct mg_mgr mgr;
char session_token[HTTP_TOKEN_LEN + 1];
static struct mg_connection connections[32];
static unsigned connection_count, ws_calls, tcp_calls, messages[2];
static char last_url[2048];
static struct mg_connection *fake_connect(const char *url, mg_event_handler_t fn, void *data) {
   assert(connection_count < 32);
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
static char last_status[4096];
static void capture(const char *window, const char *fmt, va_list ap) {
   if (!window || !strcmp(window, "status")) {
      vsnprintf(last_status, sizeof(last_status), fmt, ap);
   }
}
static const char *current_room(void) {
   return NULL;
}
static void chooser(void) {
}
static const rr_frontend_ops_t ops = {
   .vprint = capture, .chat_current_room = current_room, .show_server_chooser = chooser
};
const rr_frontend_ops_t *frontend_ops(void) {
   return &ops;
}
bool frontend_present(void) {
   return true;
}
void rrclient_update_connection_ui(int connected) {
   ws_connected = connected;
}
void tui_refresh_sb_online(void) {
}

static void message(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)event;
   (void)data;
   (void)user;
   assert(conn == ws_conn);
   ui_print(NULL, "probe");

   if (!strcmp(server_name, "alpha")) {
      messages[0]++;
      assert(rrclient_room_is_joined("#shared"));
   }

   if (!strcmp(server_name, "beta")) {
      messages[1]++;
      assert(!rrclient_room_is_joined("#shared"));
   }
}
static void auth(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)event;
   (void)data;
   (void)user;
   assert(conn == ws_conn);
   free((void *)login_user);
   login_user = strdup(server_name);
   ws_connected = 1;
}
static void receive(struct mg_connection *c, const char *json) {
   struct mg_ws_message m = {
      .data = mg_str(json), .flags = WEBSOCKET_OP_TEXT
   };
   c->fn(c, MG_EV_WS_MSG, &m);
}
int main(void) {
   event_init();
   connman_register_events();
   event_on("authorized", auth, NULL);
   event_on("irc.message", message, NULL);
   cfg = dict_new();
   dict_add(cfg, "server:alpha.server.user", "alice");
   dict_add(cfg, "server:alpha.server.pass", "alpha-secret");
   dict_add(cfg, "server:beta.server.user", "bob");
   dict_add(cfg, "server:beta.server.pass", "beta-secret");
   dict_add(cfg, "server:alpha.server.url", "ws://localhost:8420/ws/");
   assert(rrclient_connect_url("invalid", "localhost:6667"));
   assert(!connection_count);
   assert(!rrclient_connect_url("alpha", "irc://localhost"));
   assert(tcp_calls == 1 && !strcmp(last_url, "tcp://localhost:6667"));
   rrconn_t *a = ws_conn;
   struct mg_connection *ac = a->conn;
   assert(!strcmp(server_name, "alpha") && !strcmp(a->nick, "alice"));
   assert(!strncmp(last_status, "|alpha| ", 8));
   const char *alpha_window = rrclient_window_name("#shared");
   assert(rrclient_room_join("#shared"));
   assert(rrclient_room_set_topic("#shared", "alpha topic"));
   assert(!rrclient_connect_url("beta", "ircs://[::1]"));
   rrconn_t *b = ws_conn;
   struct mg_connection *bc = b->conn;
   assert(a != b && !ac->is_closing && !strcmp(last_url, "tls://[::1]:6697"));
   assert(strcmp(alpha_window, rrclient_window_name("#shared")));
   assert(!rrclient_room_is_joined("#shared"));
   assert(rrclient_room_join("#beta"));
   assert(!rrclient_connection_cycle_status(false) && ws_conn == b);
   assert(rrclient_connection_cycle_status(true) && ws_conn == a);
   assert(!strcmp(rrclient_room_topic("#shared"), "alpha topic"));
   assert(!rrclient_room_is_joined("#beta"));
   a->connected = b->connected = now;
   char line[] = ":nick!user@host PRIVMSG #shared :hello\r\n";
   mg_iobuf_add(&bc->recv, 0, line, sizeof(line) - 1);
   bc->fn(bc, MG_EV_READ, NULL);
   assert(messages[1] == 1 && ws_conn == a && !strcmp(server_name, "alpha"));
   assert(!strcmp(last_status, "|beta| probe"));
   mg_iobuf_add(&ac->recv, 0, line, sizeof(line) - 1);
   ac->fn(ac, MG_EV_READ, NULL);
   assert(messages[0] == 1 && ws_conn == a);
   mg_iobuf_del(&ac->send, 0, ac->send.len);
   mg_iobuf_del(&bc->send, 0, bc->send.len);
   char *join_args[] = {
      "join", "#only-alpha"
   };
   assert(!cmd_join(2, join_args));
   assert(ac->send.len == strlen("JOIN #only-alpha\r\n") && !bc->send.len);
   assert(!memcmp(ac->send.buf, "JOIN #only-alpha\r\n", ac->send.len));
   rrclient_connection_select_window(alpha_window);
   assert(ws_conn == a);

   assert(!disconnect_server("beta") && bc->is_closing && !ac->is_closing);
   bc->fn(bc, MG_EV_CLOSE, NULL);
   assert(ws_conn == a && !a->conn->is_closing);
   assert(!rrclient_connect_url("beta", "wss://example.test"));
   b = ws_conn;
   bc = b->conn;
   assert(ws_calls == 1 && !strcmp(last_url, "wss://example.test:4420/"));
   assert(!disconnect_server("alpha"));
   ac->fn(ac, MG_EV_CLOSE, NULL);
   assert(!rrclient_connect_url("alpha", "ws://localhost/ws/"));
   a = ws_conn;
   ac = a->conn;
   assert(ws_calls == 2 && !strcmp(last_url, "ws://localhost:8420/ws/"));
   assert(!rrclient_connect_url(NULL, "ws://localhost/ws/"));
   assert(ws_conn == a && ws_calls == 2 && !strcmp(server_name, "alpha"));
   receive(bc, "{\"msg.type\":\"auth\",\"auth.cmd\":\"challenge\",\"auth.user\":\"bob\",\"auth.nonce\":\"nonce-b\",\"auth.token\":\"token-b\"}");
   receive(ac, "{\"msg.type\":\"auth\",\"auth.cmd\":\"challenge\",\"auth.user\":\"alice\",\"auth.nonce\":\"nonce-a\",\"auth.token\":\"token-a\"}");
   assert(!strcmp(a->token, "token-a") && !strcmp(b->token, "token-b"));
   receive(bc, "{\"msg.type\":\"auth\",\"auth.cmd\":\"authorized\",\"auth.user\":\"bob\"}");
   receive(ac, "{\"msg.type\":\"auth\",\"auth.cmd\":\"authorized\",\"auth.user\":\"alice\"}");
   assert(!strcmp(login_user, "alpha") && !strcmp(session_token, "token-a"));
   assert(rrclient_connection_cycle() && ws_conn == b);
   assert(!strcmp(login_user, "beta") && !strcmp(session_token, "token-b"));
   ac->fn(ac, MG_EV_ERROR, "alpha failed");
   ac->fn(ac, MG_EV_CLOSE, NULL);
   assert(ws_conn == b && b->conn == bc && !bc->is_closing);
   now += 2;
   rrclient_poll_events_reconnect();
   assert(rrclient_connection_find("alpha") != a && ws_conn == b);
   char *server_args[] = {
      "server", "irc://adhoc.test"
   };
   assert(!cmd_server(2, server_args));
   assert(!strcmp(server_name, "adhoc.test"));
   ws_conn->conn->fn(ws_conn->conn, MG_EV_CONNECT, NULL);
   mg_iobuf_del(&ws_conn->conn->send, 0, ws_conn->conn->send.len);
   join_args[1] = "#whatever";
   assert(!cmd_join(2, join_args));
   assert(ws_conn->conn->send.len == strlen("JOIN #whatever\r\n"));
   assert(!memcmp(ws_conn->conn->send.buf, "JOIN #whatever\r\n", ws_conn->conn->send.len));
   assert(!strcmp(rrclient_connection_iter(0), "alpha"));
   assert(!strcmp(rrclient_connection_iter(1), "beta"));
   assert(!strcmp(rrclient_connection_iter(2), "adhoc.test"));
   assert(!rrclient_connection_iter(3));

   dict_add(cfg, "server.proxy", "socks5h://localhost:1080");
   assert(!rrclient_connect_url(NULL, "irc://proxied.test"));
   assert(!strcmp(last_url, "tcp://localhost:1080"));
   struct mg_connection *pc = ws_conn->conn;
   assert(!pc->send.len && !pc->is_tls);
   pc->fn(pc, MG_EV_CONNECT, NULL);
   assert(pc->send.len == 3 && !memcmp(pc->send.buf, "\5\1\0", 3));
   mg_iobuf_del(&pc->send, 0, pc->send.len);
   mg_iobuf_add(&pc->recv, 0, "\5\0", 2);
   pc->fn(pc, MG_EV_READ, NULL);
   assert(pc->send.len == 19 && !memcmp(pc->send.buf + 5, "proxied.test", 12));
   mg_iobuf_del(&pc->send, 0, pc->send.len);
   mg_iobuf_add(&pc->recv, 0, "\5\0\0\1\0\0\0\0\0\0", 10);
   pc->fn(pc, MG_EV_READ, NULL);
   assert(ws_conn->sent_login && pc->send.len > 3);
   assert(!disconnect_server("proxied.test"));
   pc->fn(pc, MG_EV_CLOSE, NULL);
   assert(!rrclient_connect_url(NULL, "irc://proxied.test"));
   assert(!strcmp(last_url, "tcp://localhost:1080") && !ws_conn->sent_login);
   pc = ws_conn->conn;
   pc->fn(pc, MG_EV_CONNECT, NULL);
   assert(pc->send.len == 3);
   pc->fn(pc, MG_EV_ERROR, "proxy lost");
   pc->fn(pc, MG_EV_CLOSE, NULL);
   /* The headless fixture omits m_privmsg.c, which maps this IRC event. */
   event_emit("disconnected", ws_conn, NULL);
   unsigned before_retry = connection_count;
   now += 2;
   rrclient_poll_events_reconnect();
   assert(connection_count > before_retry && !strcmp(last_url, "tcp://localhost:1080"));
   assert(!ws_conn->sent_login && !ws_conn->conn->send.len);
   dict_add(cfg, "server:direct.server.proxy", "");
   assert(!rrclient_connect_url("direct", "irc://direct.test"));
   assert(!strcmp(last_url, "tcp://direct.test:6667"));
   dict_add(cfg, "server:broken.server.proxy", "socks4://localhost");
   before_retry = connection_count;
   assert(rrclient_connect_url("broken", "irc://broken.test"));
   assert(connection_count == before_retry);

   for (unsigned i = 0 ; i < connection_count ; i++) {
      mg_iobuf_free(&connections[i].recv);
      mg_iobuf_free(&connections[i].send);
   }

   for (unsigned i = 0 ; rrclient_connection_iter(i) ; i++) {
      rrconn_t *c = rrclient_connection_find(rrclient_connection_iter(i));

      if (c && c->conn) {
         c->conn->fn(c->conn, MG_EV_CLOSE, NULL);
      }
   }

   connman_shutdown();
   assert(!ws_conn && !server_name && !login_user);
   dict_free(cfg);
   cfg = NULL;
   event_shutdown();
   puts("PASS: simultaneous transports, isolated rooms/auth, selection, defaults, independent disconnect/reconnect and ad hoc names");

   return 0;
}

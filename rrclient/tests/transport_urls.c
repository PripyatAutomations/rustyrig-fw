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

/* Reuse the real PART listener while this headless probe supplies its own connection/status rendering hooks. Unused event handlers are discarded. */
#define rrclient_update_connection_ui unused_event_connection_ui
#define tui_refresh_sb_online unused_event_sb_online
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <rrclient/events.c>
#pragma GCC diagnostic pop
#undef rrclient_update_connection_ui
#undef tui_refresh_sb_online

bool userlist_remove_by_name_room(const char *name, const char *room) {
   (void)name;
   (void)room;

   return true;
}
static unsigned parted_media_rooms;
void rrclient_media_room_parted(const char *room) {
   assert(!strcmp(room, "#closeme"));
   parted_media_rooms++;
}

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
static char last_status[4096], server_listing[16384];
static const char *active_room;
static bool status_active = true;
static void capture(const char *window, const char *fmt, va_list ap) {
   if (!window || !strcmp(window, "status")) {
      vsnprintf(last_status, sizeof(last_status), fmt, ap);
      strlcat(server_listing, last_status, sizeof(server_listing));
      strlcat(server_listing, "\n", sizeof(server_listing));
   }
}
static const char *current_room(void) {
   return active_room;
}
static bool current_status(void) {
   return status_active;
}
static unsigned closed_status_tabs;
static void remove_status_tab(const char *room) {
   assert(strcasecmp(room, "status"));
   closed_status_tabs++;

   if (!strcmp(room, "server.notice")) {
      assert(!strcmp(server_name, "beta"));
   } else {
      active_room = NULL;
      status_active = true;
   }
}
static bool tui_only;
static unsigned chooser_calls;
static void chooser(void) {
   chooser_calls++;
}
static const rr_frontend_ops_t ops = {
   .vprint = capture, .chat_current_room = current_room, .show_server_chooser = chooser, .chat_room_remove = remove_status_tab, .chat_status_active =
      current_status
};
const rr_frontend_ops_t *frontend_ops(void) {
   return tui_only ? NULL : &ops;
}
bool frontend_present(void) {
   return !tui_only;
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
   ui_mode = UI_MODE_GTK;
   event_init();
   connman_register_events();
   event_on("authorized", auth, NULL);
   event_on("irc.message", message, NULL);
   event_on("client.server.status.close", ui_server_status_close, NULL);
   event_on("part", rrclient_handle_part, NULL);
   cfg = dict_new();
   dict_add(cfg, "server:alpha.server.user", "alice");
   dict_add(cfg, "server:alpha.server.pass", "alpha-secret");
   dict_add(cfg, "server:beta.server.user", "bob");
   dict_add(cfg, "server:beta.server.pass", "beta-secret");
   dict_add(cfg, "server:alpha.server.url", "ws://localhost:8420/ws/");
   char *picker_args[] = {
      "server"
   };
   assert(cmd_server(1, picker_args) && chooser_calls == 1 && !connection_count);
   char *unknown_args[] = {
      "server", "libera"
   };
   config_file = "./config/rrclient.cfg";
   assert(cmd_server(2, unknown_args) && !connection_count);
   assert(strstr(last_status, "No server.url for profile 'libera'") && strstr(last_status, config_file));
   dict_add(cfg, "server:libera.server.url", "irc://irc.libera.chat:6667/");
   dict_add(cfg, "server:libera.server.user", "w00kien00kie");
   dict_add(cfg, "server:libera.server.proxy", "socks5h://localhost:8111");
   assert(!cmd_server(2, unknown_args));
   assert(!strcmp(last_url, "tcp://localhost:8111"));
   assert(!strcmp(ws_conn->nick, "w00kien00kie") && !strcmp(server_name, "libera"));
   connman_shutdown();

   for (unsigned i = 0 ; i < connection_count ; i++) {
      mg_iobuf_free(&connections[i].recv);
      mg_iobuf_free(&connections[i].send);
   }

   memset(connections, 0, sizeof(connections));
   connection_count = tcp_calls = 0;
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

   assert(rrclient_connection_select("beta"));
   rrclient_server_status_window("server.notice");
   rrclient_server_status_window("status");
   assert(rrclient_connection_select("alpha"));
   assert(!disconnect_server("beta") && bc->is_closing && !ac->is_closing);
   bc->fn(bc, MG_EV_CLOSE, NULL);
   assert(ws_conn == a && !a->conn->is_closing && closed_status_tabs == 1);
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
   dict_add(cfg, "server.proxy", ""); /* A retry must keep its original proxy. */
   unsigned before_retry = connection_count;
   now += 2;
   rrclient_poll_events_reconnect();
   assert(connection_count > before_retry && !strcmp(last_url, "tcp://localhost:1080"));
   assert(!ws_conn->sent_login && !ws_conn->conn->send.len);
   dict_add(cfg, "server.proxy", "socks5h://localhost:1080");
   dict_add(cfg, "server:direct.server.proxy", "");
   assert(!rrclient_connect_url("direct", "irc://direct.test"));
   assert(!strcmp(last_url, "tcp://localhost:1080"));
   dict_add(cfg, "server:broken.server.proxy", "socks4://localhost");
   before_retry = connection_count;
   assert(rrclient_connect_url("broken", "irc://broken.test"));
   assert(connection_count == before_retry);

   dict_add(cfg, "server.proxy", "");
   before_retry = connection_count;
   char *missing_proxy[] = {
      "server", "-proxy"
   };
   assert(cmd_server(2, missing_proxy) && connection_count == before_retry);
   char *invalid_proxy[] = {
      "server", "-proxy", "socks4://localhost", "irc://invalid-option.test"
   };
   assert(cmd_server(4, invalid_proxy) && connection_count == before_retry);
   dict_add(cfg, "server:override.server.url", "irc://override.test");
   dict_add(cfg, "server:override.server.proxy", "socks5h://localhost:1082");
   dict_add(cfg, "server.proxy", "socks5h://localhost:1081");
   char *override_args[] = {
      "server", "-proxy", "socks5h://localhost:1083", "override"
   };
   assert(!cmd_server(4, override_args));
   assert(!strcmp(last_url, "tcp://localhost:1083"));
   assert(!disconnect_server("override"));
   ws_conn->conn->fn(ws_conn->conn, MG_EV_CLOSE, NULL);
   assert(!connect_server("override"));
   assert(!strcmp(last_url, "tcp://localhost:1083"));
   assert(!rrclient_connect_url("section", "irc://section.test"));
   assert(!strcmp(last_url, "tcp://localhost:1081"));
   dict_add(cfg, "server:section.server.proxy", "socks5h://localhost:1082");
   assert(!disconnect_server("section"));
   ws_conn->conn->fn(ws_conn->conn, MG_EV_CLOSE, NULL);
   assert(!rrclient_connect_url("section", "irc://section.test"));
   assert(!strcmp(last_url, "tcp://localhost:1082"));
   dict_add(cfg, "server.proxy", "");
   assert(!rrclient_connect_url(NULL, "irc://standalone-direct.test"));
   assert(!strcmp(last_url, "tcp://standalone-direct.test:6667"));
   char *proxy_args[] = {
      "server", "ircs://inherited.test", "-proxy", "socks5h://localhost:1080"
   };
   assert(!cmd_server(4, proxy_args));
   assert(!strcmp(last_url, "tcp://localhost:1080") && !ws_conn->sent_login);
   struct mg_connection *failed = ws_conn->conn;
   failed->fn(failed, MG_EV_CONNECT, NULL);
   mg_iobuf_add(&failed->recv, 0, "\5\377", 2);
   failed->fn(failed, MG_EV_READ, NULL);
   assert(failed->is_closing && !ws_conn->sent_login && !failed->is_tls);
   before_retry = connection_count;
   failed->fn(failed, MG_EV_CLOSE, NULL);
   assert(!rrclient_connect_url("inherited.test", "ircs://inherited.test"));
   assert(connection_count == before_retry + 1 && !strcmp(last_url, "tcp://localhost:1080"));
   /* Closing an online room queues PART and waits for server confirmation. */
   assert(!rrclient_connect_url("window", "irc://window.test"));
   struct mg_connection *window_connection = ws_conn->conn;
   window_connection->fn(window_connection, MG_EV_CONNECT, NULL);
   event_emit("irc.connected", ws_conn, NULL);
   active_room = "#closeme";
   status_active = false;
   assert(rrclient_room_join(active_room));
   mg_iobuf_del(&window_connection->send, 0, window_connection->send.len);
   char *win_close[] = {
      "win", "close"
   };
   unsigned before_close = closed_status_tabs;
   assert(!cmd_win(2, win_close));
   assert(window_connection->send.len == strlen("PART #closeme\r\n") &&
      !memcmp(window_connection->send.buf, "PART #closeme\r\n", window_connection->send.len));
   assert(closed_status_tabs == before_close && rrclient_room_is_joined("#closeme"));
   /* Existing self-PART event handling then removes the membership and tab. */
   dict *part_confirmation = dict_new();
   dict_add(part_confirmation, "talk.room", "#closeme");
   dict_add(part_confirmation, "talk.user", ws_conn->nick);
   event_emit_dict("part", ws_conn, part_confirmation);
   dict_free(part_confirmation);
   assert(!rrclient_room_is_joined("#closeme") && closed_status_tabs == before_close + 1 && parted_media_rooms == 1);
   active_room = "#failed";
   status_active = false;
   assert(rrclient_room_join(active_room));
   window_connection->is_closing = 1;
   assert(cmd_win(2, win_close) && closed_status_tabs == before_close + 1);
   assert(rrclient_room_is_joined("#failed"));
   window_connection->is_closing = 0;
   active_room = "alice";
   mg_iobuf_del(&window_connection->send, 0, window_connection->send.len);
   assert(!cmd_win(2, win_close) && !window_connection->send.len);
   assert(closed_status_tabs == before_close + 2);
   assert(cmd_win(2, win_close)); /* Shared status cannot be closed. */
   char *win_missing[] = {
      "win"
   };
   assert(cmd_win(1, win_missing));

   rrclient_rooms_clear();
   assert(rrclient_room_join("#offline"));
   rrclient_rooms_disconnect();
   ws_connected = 0;
   active_room = "#offline";
   status_active = false;
   assert(!cmd_win(2, win_close));
   ws_connected = 1;
   rrclient_rooms_rejoin_available();
   assert(!window_connection->send.len); /* Closed offline room must not rejoin. */

   active_room = "#stay";
   status_active = false;
   char *disconnect_current[] = {
      "disconnect"
   };
   assert(cmd_disconnect(1, disconnect_current) && !window_connection->is_closing);
   assert(!rrclient_connect_url("named-disconnect", "irc://named.test"));
   struct mg_connection *named_connection = ws_conn->conn;
   assert(rrclient_connection_select("window"));
   char *disconnect_named[] = {
      "disconnect", "named-disconnect"
   };
   assert(!cmd_disconnect(2, disconnect_named));
   assert(named_connection->is_closing && !window_connection->is_closing && !strcmp(rrclient_selected_server(), "window"));
   char *disconnect_unknown[] = {
      "disconnect", "does-not-exist"
   };
   assert(cmd_disconnect(2, disconnect_unknown) && !window_connection->is_closing);
   server_listing[0] = '\0';
   active_room = NULL;
   status_active = true;
   assert(cmd_server(1, picker_args));
   assert(strstr(server_listing, "Configured servers:") && strstr(server_listing, "alpha - ws://localhost:8420/ws/"));
   assert(strstr(server_listing, "Connected servers") && strstr(server_listing, "window - connected (selected)"));
   assert(!cmd_disconnect(1, disconnect_current) && window_connection->is_closing);

   dict_add(cfg, "server:quit-one.server.proxy", "");
   dict_add(cfg, "server:quit-two.server.proxy", "");
   assert(!rrclient_connect_url("quit-one", "irc://quit-one.test"));
   struct mg_connection *q1 = ws_conn->conn;
   q1->fn(q1, MG_EV_CONNECT, NULL);
   assert(!rrclient_connect_url("quit-two", "irc://quit-two.test"));
   struct mg_connection *q2 = ws_conn->conn;
   q2->fn(q2, MG_EV_CONNECT, NULL);
   mg_iobuf_del(&q1->send, 0, q1->send.len);
   mg_iobuf_del(&q2->send, 0, q2->send.len);
   char *quit_args[] = {
      "QUIT", "-y", "leaving", "now"
   };
   assert(!cmd_quit(4, quit_args) && dying);
   const char *quit_line = "QUIT :leaving now\r\n";
   assert(q1->is_draining && q2->is_draining);
   assert(q1->send.len == strlen(quit_line) && !memcmp(q1->send.buf, quit_line, strlen(quit_line)));
   assert(q2->send.len == strlen(quit_line) && !memcmp(q2->send.buf, quit_line, strlen(quit_line)));
   rrclient_quit_servers("Client exiting");
   assert(q1->send.len == strlen(quit_line) && q2->send.len == strlen(quit_line));

   /* Explicit disconnect removes an owned notice window in the TUI as well. */
   tui_only = true;
   ui_mode = UI_MODE_TUI;
   tui_window_t *shared_status = tui_window_create("status");
   assert(shared_status);
   const char *notice_key = rrclient_window_name("server.notice");
   char saved_notice_key[64];
   snprintf(saved_notice_key, sizeof(saved_notice_key), "%s", notice_key);
   assert(tui_window_create(notice_key));
   rrclient_server_status_window("server.notice");
   rrclient_server_status_window("status");
   const char *query_key = rrclient_window_name("bob");
   char saved_query_key[64];
   snprintf(saved_query_key, sizeof(saved_query_key), "%s", query_key);
   assert(tui_window_create(query_key) && tui_window_focus(query_key));
   assert(!cmd_win(2, win_close));
   assert(!tui_window_find(saved_query_key));
   tui_window_focus("status");
   assert(cmd_win(2, win_close) && tui_window_find("status") == shared_status);
   assert(!disconnect_server("quit-two"));
   assert(!tui_window_find(saved_notice_key) && tui_window_find("status") == shared_status);
   tui_only = false;

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

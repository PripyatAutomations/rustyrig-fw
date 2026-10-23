#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now = 100;
bool dying, restarting;
static unsigned tls_calls;
void mg_tls_init(struct mg_connection *conn, const struct mg_tls_opts *opts) {
   assert(conn->is_tls);
   assert(!mg_strcmp(opts->name, mg_str("chat.example.test")));
   tls_calls++;
}
static void check_registration(bool tls) {
   server_cfg_t server = {
      0
   };
   snprintf(server.host, sizeof(server.host), "chat.example.test");
   snprintf(server.pass, sizeof(server.pass), "secret");
   rrconn_t client = {
      .fd = -1, .server = &server
   };
   snprintf(client.nick, sizeof(client.nick), "tester");
   struct mg_connection connection = {
      .fn_data = &client, .is_tls = tls
   };
   irc_mongoose_handler(&connection, MG_EV_OPEN, NULL);
   irc_mongoose_handler(&connection, MG_EV_CONNECT, NULL);

   if (tls) {
      assert(!client.sent_login && !connection.send.len && tls_calls == 1);
      irc_mongoose_handler(&connection, MG_EV_TLS_HS, NULL);
   }
   assert(client.sent_login && client.connected == now);
   const char *registration = "PASS secret\r\nCAP LS 302\r\nNICK tester\r\nUSER tester 0 * :tester\r\n";
   assert(connection.send.len == strlen(registration));
   assert(!memcmp(connection.send.buf, registration, strlen(registration)));
   const char *ping = "PING :probe\r\n";
   assert(mg_iobuf_add(&connection.recv, 0, ping, strlen(ping)) == strlen(ping));
   irc_mongoose_handler(&connection, MG_EV_READ, NULL);
   assert(!connection.recv.len);
   assert(connection.send.len == strlen(registration) + strlen("PONG :probe\r\n"));
   assert(!memcmp(connection.send.buf + strlen(registration), "PONG :probe\r\n", strlen("PONG :probe\r\n")));
   dict *talk = dict_new();
   dict_add(talk, "msg.type", "talk");
   dict_add(talk, "talk.cmd", "msg");
   dict_add(talk, "talk.target", "#room");
   dict_add(talk, "talk.data", "hello");
   size_t previous = connection.send.len;
   assert(ws_send_dict(NULL, &client, talk, WEBSOCKET_OP_TEXT));
   const char *privmsg = "PRIVMSG #room :hello\r\n";
   assert(connection.send.len == previous + strlen(privmsg));
   assert(!memcmp(connection.send.buf + previous, privmsg, strlen(privmsg)));
   dict_add(talk, "msg.type", "cat");
   previous = connection.send.len;
   assert(!ws_send_dict(NULL, &client, talk, WEBSOCKET_OP_TEXT));
   assert(connection.send.len == previous);
   dict_free(talk);
   char nul[] = {
      'X', '\0', '\r', '\n'
   };
   assert(mg_iobuf_add(&connection.recv, 0, nul, sizeof(nul)) == sizeof(nul));
   irc_mongoose_handler(&connection, MG_EV_READ, NULL);
   assert(connection.is_closing);
   irc_mongoose_handler(&connection, MG_EV_CLOSE, NULL);
   assert(!client.conn && !client.connected && !client.sent_login);
   mg_iobuf_free(&connection.send);
   mg_iobuf_free(&connection.recv);
}
int main(void) {
   event_init();
   assert(!irc_init());
   check_registration(false);
   check_registration(true);
   irc_shutdown();
   event_shutdown();
   puts("PASS: IRC Mongoose TCP/TLS lifecycle, registration ordering, plain IRC output and input validation");

   return 0;
}

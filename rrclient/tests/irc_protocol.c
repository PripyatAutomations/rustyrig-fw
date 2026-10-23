#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now = 100;
bool dying, restarting;
static unsigned messages, privmsgs, unknowns, topics, redirects;
static char last_event[4096];
static void observe(const char *event, const char *data, rrconn_t *conn, void *user) {
   (void)conn;
   (void)user;
   assert(data && data[0] == '{');

   if (!strcmp(event, "irc.message")) {
      messages++;
   } else if (!strcmp(event, "irc.privmsg")) {
      privmsgs++;
   } else if (!strcmp(event, "irc.unsupported")) {
      unknowns++;
   } else if (!strcmp(event, "irc.topic")) {
      topics++;
   }

   if (!strcmp(event, "irc.channel.redirect")) {
      dict *d = json2dict(data);
      assert(d && !strcmp(dict_get(d, "msg.arg2", ""), "#old"));
      assert(!strcmp(dict_get(d, "msg.arg3", ""), "#new"));
      assert(!strcmp(dict_get(d, "msg.arg4", ""), "Forwarding"));
      dict_free(d);
      redirects++;
   }
   snprintf(last_event, sizeof(last_event), "%s", data);
}

static unsigned custom_calls;
static bool custom(irc_conn_t *conn, irc_message_t *message) {
   (void)conn;
   assert(message->argc == 1);
   custom_calls++;

   return false;
}
static void parser_tests(rrconn_t *conn) {
   assert(!irc_parse_message(NULL));
   assert(!irc_parse_message(""));
   assert(!irc_parse_message("   "));
   assert(!irc_parse_message(":prefix"));
   assert(!irc_parse_message(": PING :token"));
   assert(!irc_parse_message("PING :x\r\nNICK attacker"));
   assert(!irc_parse_message("CMD 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"));
   irc_message_t *mp = irc_parse_message(":nick!u@h PRIVMSG #room :hello world");
   assert(mp && mp->argc == 3 && !strcmp(mp->prefix, "nick!u@h"));
   assert(!strcmp(mp->argv[2], "hello world") && !mp->argv[3]);
   irc_message_free(mp);
   assert(irc_dispatch_message(conn, NULL));
   rr_irc_callback_t first = {
      .cmd = "CUSTOM", .cb = custom
   };
   rr_irc_callback_t second = {
      .cmd = "CUSTOM", .cb = custom
   };
   assert(!irc_register_callback(&first));
   assert(!irc_register_callback(&first));
   assert(!irc_register_callback(&second));
   assert(!irc_process_message(conn, "custom"));
   assert(custom_calls == 1);
   assert(!irc_remove_callback(&first));
   assert(!irc_process_message(conn, "CUSTOM"));
   assert(custom_calls == 2);
   assert(!irc_remove_callback(&second));
   assert(irc_remove_callback(&second));
   assert(!strcmp(first.cmd, "CUSTOM"));
}
static void channel_tests(void) {
   irc_channel_t chan = {
      0
   };
   irc_message_t *mp = irc_parse_message(":server 353 me = #room :@alice +bob");
   handle_numeric_353(&chan, mp);
   irc_message_free(mp);
   assert(chan.users == 2 && chan_find_user(&chan, "alice")->is_op);
   mp = irc_parse_message(":alice!u@h NICK :charlie");
   handle_nick_change(&chan, mp);
   irc_message_free(mp);
   assert(!chan_find_user(&chan, "alice"));
   assert(chan_find_user(&chan, "charlie")->is_op);
   mp = irc_parse_message(":charlie!u@h QUIT");
   handle_part_or_quit(&chan, mp);
   irc_message_free(mp);
   assert(chan.users == 1);
   chan_clear_users(&chan);
}
int main(void) {
   int sockets[2];
   assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
   server_cfg_t server = {
      0
   };
   snprintf(server.nick, sizeof(server.nick), "tester");
   rrconn_t conn = {
      .fd = sockets[0], .server = &server, .connected = now
   };
   snprintf(conn.nick, sizeof(conn.nick), "%s", server.nick);
   event_init();
   event_on("irc.message", observe, NULL);
   event_on("irc.privmsg", observe, NULL);
   event_on("irc.unsupported", observe, NULL);
   event_on("irc.topic", observe, NULL);
   event_on("irc.channel.redirect", observe, NULL);
   parser_tests(&conn);
   channel_tests();
   assert(!irc_init());
   assert(!irc_init());
   irc_io_poll(&conn);
   assert(conn.sent_login);
   char reply[4096];
   ssize_t n = recv(sockets[1], reply, sizeof(reply) - 1, MSG_DONTWAIT);
   assert(n > 0);
   reply[n] = '\0';
   assert(!strcmp(reply, "CAP LS 302\r\nNICK tester\r\nUSER tester 0 * :tester\r\n"));
   assert(!irc_send(&conn, "NICK injected\r\nQUIT"));
   unsigned before = messages;
   assert(send(sockets[1], "PING :token\r", 12, 0) == 12);
   irc_io_poll(&conn);
   assert(messages == before);
   assert(send(sockets[1], "\n", 1, 0) == 1);
   irc_io_poll(&conn);
   n = recv(sockets[1], reply, sizeof(reply) - 1, MSG_DONTWAIT);
   assert(n > 0);
   reply[n] = '\0';
   assert(!strcmp(reply, "PONG :token\r\n"));
   assert(messages == before + 1);
   assert(!irc_process_message(&conn, ":server NOTICE tester :hello"));
   assert(!irc_process_message(&conn, ":nick PRIVMSG tester :hello"));
   assert(privmsgs == 1 && strstr(last_event, "hello"));
   assert(!irc_process_message(&conn, ":nick PRIVMSG tester :\001VERSION\001"));
   assert(irc_process_message(&conn, ":nick PRIVMSG tester :\001PING"));
   assert(irc_process_message(&conn, "PRIVMSG"));
   assert(!irc_process_message(&conn, ":nick QUIT"));
   assert(!irc_process_message(&conn, ":server 332 tester #room :a topic"));
   assert(topics == 1);
   assert(!irc_process_message(&conn, ":server 470 tester #old #new :Forwarding"));
   assert(redirects == 1);
   assert(irc_process_message(&conn, ":server 470 tester #old"));
   assert(irc_process_message(&conn, ":server 470 other #old #new :Wrong recipient"));
   assert(irc_process_message(&conn, ":server 470 tester #old nick :Wrong target"));
   assert(!irc_process_message(&conn, ":server 470 tester #old #OLD :Same channel"));
   assert(redirects == 1);
   assert(!irc_process_message(&conn, ":server 001 accepted :welcome"));
   assert(conn.authenticated && !strcmp(conn.nick, "accepted"));
   assert(!irc_process_message(&conn, "UNRECOGNIZED"));
   assert(unknowns == 1);
   assert(!irc_process_message(&conn, "000 tester :reserved"));
   assert(unknowns == 2);
   irc_shutdown();
   assert(!irc_init());
   irc_shutdown();
   event_shutdown();
   close(sockets[1]);
   irc_io_poll(&conn);
   assert(conn.fd == -1 && !conn.connected && !conn.authenticated);
   puts("PASS: IRC parsing, callback ownership, event payloads, registration, PING, CTCP and channel state");

   return 0;
}

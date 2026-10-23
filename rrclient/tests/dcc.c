/* Classic peer interoperability, real TCP and filesystem descriptors. */
#include <assert.h>
#include <rrclient/irc.dcc.c>
static rrconn_t alice, bob;
static unsigned incoming, completed, errors, chats;
static char chat_text[4096];
static void state(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   dict *d = json2dict(data);
   assert(d);
   const char *status = dict_get(d, "dcc.status", "");

   if (client == &bob && !strcmp(status, "offer")) {
      incoming = dict_get_uint(d, "dcc.id", 0);
   }

   if (!strcmp(status, "complete")) {
      completed++;
   }

   if (!strcmp(status, "error")) {
      errors++;
   }
   dict_free(d);
}
static void chat(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;
   dict *d = json2dict(data);
   assert(d);
   chats++;
   snprintf(chat_text, sizeof(chat_text), "%s", dict_get(d, "dcc.text", ""));
   dict_free(d);
}
static unsigned free_port(void) {
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   assert(fd >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(fd, (struct sockaddr *)&address, sizeof(address)));
   socklen_t length = sizeof(address);
   assert(!getsockname(fd, (struct sockaddr *)&address, &length));
   unsigned port = ntohs(address.sin_port);
   close(fd);

   return port;
}
static irc_dcc_options_t options(void) {
   unsigned port = free_port();

   return (irc_dcc_options_t) {
             .bind = "127.0.0.1", .host = "127.0.0.1", .port_min = port, .port_max = port
   };
}
static void deliver(int fd) {
   char wire[1024], line[1200];
   ssize_t n = recv(fd, wire, sizeof(wire) - 1, 0);
   assert(n > 0);
   wire[n] = 0;
   char *end = strchr(wire, '\r');

   if (end) {
      *end = 0;
   }
   snprintf(line, sizeof(line), ":alice!u@host %s", wire);
   incoming = 0;
   assert(!irc_process_message(&bob, line) && incoming);
}
static void polls(unsigned count) {
   for (unsigned i = 0 ; i < count ; i++) {
      irc_dcc_poll();
   }
}
static int temporary(void) {
   char path[] = "/tmp/rr-dcc-test-XXXXXX";
   int fd = mkstemp(path);
   assert(fd >= 0);
   unlink(path);

   return fd;
}
static unsigned session_count(void) {
   unsigned n = 0;
   irc_dcc_session_t info;
   while (irc_dcc_session_get(n, &info)) {
      n++;
   }
   return n;
}
static int raw_connect(unsigned id) {
   struct dcc_session *s = find(&alice, id);
   assert(s);
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   assert(fd >= 0);
   struct timeval timeout = {
      .tv_sec = 2
   };
   setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_port = htons(s->info.port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!connect(fd, (struct sockaddr *)&address, sizeof(address)));
   polls(2);

   return fd;
}
int main(void) {
   event_init();
   assert(!irc_init());
   irc_dcc_init();
   event_on(IRC_DCC_EVENT, state, NULL);
   event_on(IRC_DCC_CHAT_EVENT, chat, NULL);
   int irc[2];
   assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, irc));
   alice = (rrconn_t) {
      .fd = irc[0], .connected = true, .authenticated = true
   };
   strcpy(alice.nick, "alice");
   bob = (rrconn_t) {
      .fd = -1, .connected = true, .authenticated = true
   };
   strcpy(bob.nick, "bob");
   irc_dcc_session_t parsed;
   assert(irc_dcc_decode("\001DCC CHAT chat 2130706433 12345\001", &parsed) && !strcmp(parsed.ip, "127.0.0.1"));
   assert(irc_dcc_decode("\001DCC SEND \"file with spaces.bin\" 127.0.0.1 12345 0\001", &parsed) && parsed.size_known && !parsed.size);
   assert(irc_dcc_decode("\001DCC SEND old.bin 2130706433 12345\001", &parsed) && !parsed.size_known);
   const char *bad[] = {
      "\001DCC CHAT chat 2130706433 0\001", "\001DCC CHAT chat host 4\001", "\001DCC SEND ../file 2130706433 5 1\001",
      "\001DCC SEND \"missing quote 2130706433 5 1\001", "\001DCC SEND f 2130706433 5 -1\001",
      "\001DCC SEND f 4294967296 5 1\001", "\001DCC SEND f 2130706433 65536 1\001",
      "\001DCC SEND f 2130706433 5 9223372036854775808\001", "\001DCC SEND f 2130706433 5 1 extra\001"
   };

   for (unsigned i = 0 ; i < sizeof(bad) / sizeof(bad[0]) ; i++) {
      assert(!irc_dcc_decode(bad[i], &parsed));
   }

   irc_dcc_options_t opts = options();
   unsigned outgoing = irc_dcc_offer_chat(&alice, "bob", &opts);
   assert(outgoing);
   deliver(irc[1]);
   assert(session_count() == 2);
   struct dcc_session *pending = find(&bob, incoming);
   assert(pending && pending->socket == -1 && pending->file == -1);
   polls(4);
   assert(pending->info.state == IRC_DCC_PENDING);
   assert(!irc_dcc_accept(&alice, incoming, -1));
   assert(irc_dcc_accept(&bob, incoming, -1));
   polls(4);
   assert(irc_dcc_chat_send(&alice, outgoing, "hello peer"));
   polls(4);
   assert(chats == 1 && !strcmp(chat_text, "hello peer"));
   assert(irc_dcc_chat_send(&bob, incoming, "reply"));
   polls(4);
   assert(chats == 2 && !strcmp(chat_text, "reply"));
   assert(!irc_dcc_chat_send(&alice, outgoing, "bad\nline"));
   assert(irc_dcc_close(&bob, incoming));
   polls(3);
   assert(!session_count());
   /* Binary files span many blocks, and caller descriptors remain independent. */
   int source = temporary(), destination = temporary();
   unsigned char data[90000], copy[sizeof(data)];

   for (unsigned i = 0 ; i < sizeof(data) ; i++) {
      data[i] = i * 31;
   }

   assert(write(source, data, sizeof(data)) == sizeof(data));
   opts = options();
   outgoing = irc_dcc_offer_file(&alice, "bob", "file with spaces.bin", source, &opts);
   assert(outgoing);
   close(source);
   deliver(irc[1]);
   assert(irc_dcc_accept(&bob, incoming, destination));

   for (unsigned i = 0 ; i < 5000 && session_count() ; i++) {
      polls(1);
      usleep(100);
   }

   assert(!session_count() && completed == 2 && !errors);
   assert(pread(destination, copy, sizeof(copy), 0) == sizeof(copy) && !memcmp(data, copy, sizeof(copy)));
   close(destination);
   /* Classic zero-length files and fragmented four-byte acknowledgements. */
   source = temporary();
   destination = temporary();
   opts = options();
   outgoing = irc_dcc_offer_file(&alice, "bob", "empty", source, &opts);
   assert(outgoing);
   deliver(irc[1]);
   assert(irc_dcc_accept(&bob, incoming, destination));
   polls(5);
   assert(completed == 4 && !session_count());
   close(destination);
   assert(write(source, data, 3) == 3);
   opts = options();
   outgoing = irc_dcc_offer_file(&alice, "bob", "fragment", source, &opts);
   assert(outgoing);
   /* Consume the IRC offer without creating a second native receiver. */
   char discard[1024];
   assert(recv(irc[1], discard, sizeof(discard), 0) > 0);
   int peer = raw_connect(outgoing);
   unsigned char bytes[3];
   assert(recv(peer, bytes, 3, MSG_WAITALL) == 3 && !memcmp(data, bytes, 3));
   uint32_t ack = htonl(3);

   for (unsigned i = 0 ; i < 4 ; i++) {
      assert(send(peer, (char *)&ack + i, 1, 0) == 1);
      polls(1);
   }

   for (unsigned i = 0 ; i < 100 && session_count() ; i++) {
      polls(1);
      usleep(1000);
   }

   assert(completed == 5 && !session_count());
   close(peer);
   close(source);
   /* Raw peers use LF or CRLF, including split/multiple lines and CTCP ACTION. */
   opts = options();
   outgoing = irc_dcc_offer_chat(&alice, "bob", &opts);
   assert(outgoing);
   assert(recv(irc[1], discard, sizeof(discard), 0) > 0);
   peer = raw_connect(outgoing);
   assert(send(peer, "split", 5, 0) == 5);
   polls(1);
   unsigned before = chats;
   const char *tail = " line\r\nsecond\n\001ACTION waves\001\n";
   assert(send(peer, tail, strlen(tail), 0) == (ssize_t) strlen(tail));

   for (unsigned i = 0 ; i < 100 && chats < before + 3 ; i++) {
      polls(1);
      usleep(1000);
   }

   assert(chats == before + 3 && !strcmp(chat_text, "\001ACTION waves\001"));
   assert(send(peer, "bad\0line\n", 9, 0) == 9);
   polls(3);
   assert(errors == 1 && !session_count());
   close(peer);
   /* Forged acknowledgements, early EOF, and disk errors never report success. */
   source = temporary();
   assert(write(source, data, 5) == 5);
   opts = options();
   outgoing = irc_dcc_offer_file(&alice, "bob", "bad-ack", source, &opts);
   assert(outgoing);
   assert(recv(irc[1], discard, sizeof(discard), 0) > 0);
   peer = raw_connect(outgoing);
   ack = htonl(6);
   assert(send(peer, &ack, 4, 0) == 4);
   polls(3);
   assert(errors == 2 && !session_count());
   close(peer);
   int listener = socket(AF_INET, SOCK_STREAM, 0);
   assert(listener >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, 1));
   socklen_t address_size = sizeof(address);
   assert(!getsockname(listener, (struct sockaddr *)&address, &address_size));
   char request[512];
   snprintf(request, sizeof(request), ":alice!u@host PRIVMSG bob :\001DCC SEND partial 2130706433 %u 10\001", ntohs(address.sin_port));
   assert(!irc_process_message(&bob, request));
   destination = temporary();
   assert(irc_dcc_accept(&bob, incoming, destination));
   peer = accept(listener, NULL, NULL);
   assert(peer >= 0);
   close(listener);
   polls(1);
   assert(send(peer, data, 3, 0) == 3);
   polls(1);
   assert(recv(peer, &ack, 4, MSG_WAITALL) == 4 && ntohl(ack) == 3);
   close(peer);
   polls(3);
   assert(errors == 3 && !session_count());
   assert(pread(destination, copy, sizeof(copy), 0) == 3);
   close(destination);
   /* Legacy peers omit size; EOF completes only after received bytes are saved. */
   listener = socket(AF_INET, SOCK_STREAM, 0);
   assert(listener >= 0);
   address.sin_port = 0;
   assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, 1));
   address_size = sizeof(address);
   assert(!getsockname(listener, (struct sockaddr *)&address, &address_size));
   snprintf(request, sizeof(request), ":alice!u@host PRIVMSG bob :\001DCC SEND legacy 2130706433 %u\001", ntohs(address.sin_port));
   assert(!irc_process_message(&bob, request));
   destination = temporary();
   assert(irc_dcc_accept(&bob, incoming, destination));
   peer = accept(listener, NULL, NULL);
   assert(peer >= 0);
   polls(1);
   assert(send(peer, data, 3, 0) == 3);
   polls(1);
   assert(recv(peer, &ack, 4, MSG_WAITALL) == 4 && ntohl(ack) == 3);
   close(peer);
   polls(3);
   assert(completed == 6 && !session_count());
   assert(pread(destination, copy, sizeof(copy), 0) == 3 && !memcmp(data, copy, 3));
   close(destination);
   /* Inject a failed destination descriptor after consent, before any write. */
   assert(!irc_process_message(&bob, request));
   destination = temporary();
   assert(irc_dcc_accept(&bob, incoming, destination));
   peer = accept(listener, NULL, NULL);
   assert(peer >= 0);
   close(listener);
   polls(1);
   struct dcc_session *failed = find(&bob, incoming);
   assert(failed);
   close(failed->file);
   failed->file = -1;
   assert(send(peer, data, 3, 0) == 3);
   polls(3);
   assert(errors == 4 && !session_count());
   assert(pread(destination, copy, sizeof(copy), 0) == 0);
   close(destination);
   close(peer);
   /* The 32-bit classic acknowledgement wraps while accounting stays 64-bit. */
   opts = options();
   outgoing = irc_dcc_offer_file(&alice, "bob", "wrap", source, &opts);
   assert(outgoing);
   assert(recv(irc[1], discard, sizeof(discard), 0) > 0);
   peer = raw_connect(outgoing);
   struct dcc_session *wrapped = find(&alice, outgoing);
   assert(wrapped);
   wrapped->info.size = wrapped->info.transferred = UINT64_C(0x100000002);
   wrapped->info.acknowledged = UINT64_C(0xfffffffd);
   ack = htonl(2);
   assert(send(peer, &ack, 4, 0) == 4);
   polls(3);
   assert(completed == 7 && !session_count());
   close(peer);
   close(source);
   /* Pending cap, expiration and session loss release listeners and descriptors. */
   char offer[] = ":alice!u@host PRIVMSG bob :\001DCC CHAT chat 2130706433 12345\001";

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS + 3 ; i++) {
      assert(!irc_process_message(&bob, offer));
   }

   assert(session_count() == IRC_DCC_MAX_SESSIONS);

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      sessions[i].deadline = mono_us() - 1;
   }

   polls(1);
   assert(!session_count());
   assert(!irc_process_message(&bob, offer));
   assert(session_count() == 1);
   event_emit("irc.disconnected", &bob, "");
   assert(!session_count());
   assert(!irc_process_message(&bob, ":alice!u@host PRIVMSG #public :\001DCC CHAT chat 2130706433 12345\001"));
   assert(!session_count());
   irc_dcc_shutdown();
   irc_shutdown();
   event_shutdown();
   close(irc[0]);
   close(irc[1]);
   puts("PASS: classic DCC parsing, consent, TCP chat, binary and empty files, fragmented ACKs, malformed peers, expiry and disconnect cleanup");
}

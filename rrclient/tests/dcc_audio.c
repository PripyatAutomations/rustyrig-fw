/* Two authenticated IRC clients negotiate independent voice attachments over UDP. */
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <librrprotocol/irc.audio.session.c>
#define offered client_offer_notice
#include <rrclient/dcc.c>
#undef offered
rrconn_t * ws_conn;
void Log(logpriority_t level, const char *subsystem, const char *format, ...) {
   (void)level;
   (void)subsystem;
   (void)format;
}
bool ui_print(const char *window, const char *format, ...) {
   (void)window;
   (void)format;

   return false;
}
void rrclient_connection_dispatch(rrconn_t *client, void (*callback) (void *), void *data) {
   callback(data);
}
const char *ui_active_window_name(void) {
   return "#voice";
}
const char *rrclient_connection_name(const rrconn_t *client) {
   return "test";
}

static unsigned pcm_count;
static void pcm(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   assert(length == sizeof(irc_audio_pcm_t));
   const irc_audio_pcm_t *pcm = data;
   assert(pcm->attachment && pcm->length == 640 && ((const uint8_t *)pcm->samples)[0] == 7);
   pcm_count++;
}
static unsigned count_peers(void) {
   unsigned n = 0;

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      n += peers[i].control != NULL;
   }

   return n;
}
int main(void) {
   bool encrypted = getenv("RR_RTP_TEST_ENCRYPTED") != NULL;

   if (!rr_rtp_supported(encrypted)) {
      puts("SKIP: RTP is unavailable");

      return 0;
   }
   cfg = dict_new();
   assert(cfg);
   dict_add(cfg, "dcc.audio.host", "127.0.0.1");
   dict_add_bool(cfg, "dcc.audio.encrypt", encrypted);
   int sockets[2];
   assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
   rrconn_t alice = {
      .fd = sockets[0], .connected = true, .authenticated = true
   };
   rrconn_t bob = {
      .fd = -1, .connected = true, .authenticated = true
   };
   strcpy(alice.nick, "alice");
   strcpy(bob.nick, "bob");
   event_init();
   assert(!irc_init());
   rrclient_dcc_register_events();
   event_on_binary(IRC_AUDIO_PCM_EVENT, pcm, NULL);
   ws_conn = &alice;
   char *audio[] = {
      "dcc", "audio", "bob"
   };
   assert(!cmd_dcc(3, audio) && count_peers() == 2);
   assert(!peers[0].local_tx && peers[1].local_tx);
   assert(strcmp(dict_get(peers[0].offer, "media.direct-key", ""), dict_get(peers[1].offer, "media.direct-key", "")));

   for (unsigned i = 0 ; i < 2 ; i++) {
      unsigned port;
      char ip[64];
      assert(sscanf(dict_get(peers[i].offer, "media.direct", ""), "rtp://%63[^:]:%u", ip, &port) == 2);
      assert(port >= 10000 && port <= 10099);
   }

   struct timeval timeout = {
      .tv_sec = 2
   };
   setsockopt(sockets[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
   char wire[2048];
   ssize_t length = recv(sockets[1], wire, sizeof(wire) - 1, 0);
   assert(length > 0);
   wire[length] = 0;
   char *save, *line = strtok_r(wire, "\r\n", &save);
   unsigned offers = 0;
   while (line) {
      char incoming[2048];
      snprintf(incoming, sizeof(incoming), ":alice!u@host %s", line);
      assert(!irc_process_message(&bob, incoming));
      offers++;
      line = strtok_r(NULL, "\r\n", &save);
   }
   assert(offers == 2 && count_peers() == 4);
   char *list[] = {
      "dcc"
   };
   assert(!cmd_dcc(1, list));
   irc_audio_session_t listed;
   assert(irc_audio_session_get(2, &listed) && !listed.accepted && !listed.outgoing);
   assert(!peers[2].attachment && !peers[3].attachment);
   uint8_t samples[640];
   memset(samples, 7, sizeof(samples));
   rrclient_dcc_capture(samples, sizeof(samples));
   rrclient_dcc_poll();
   assert(!pcm_count);
   ws_conn = &bob;
   char *accept[] = {
      "dcc", "audio", "accept", "alice"
   };
   assert(!cmd_dcc(4, accept));

   for (unsigned i = 0 ; i < 4 ; i++) {
      rrclient_dcc_poll();
   }

   for (unsigned i = 0 ; i < 4 ; i++) {
      assert(rr_rtp_socket_active(peers[i].attachment, mono_us()));
   }

   rrclient_dcc_capture(samples, sizeof(samples));
   rrclient_dcc_poll();
   assert(pcm_count == 2);
   dict_add_bool(cfg, "dcc.audio.mute", true);
   rrclient_dcc_capture(samples, sizeof(samples));
   rrclient_dcc_poll();
   assert(pcm_count == 2);
   /* Explicit direction is relative to the local microphone, not the inviter's. */
   char *close_rx[] = {
      "dcc", "audio", "close", "alice", "rx"
   };
   assert(!cmd_dcc(5, close_rx));
   assert(count_peers() == 3);
   char *close_all[] = {
      "dcc", "audio", "close", "alice"
   };
   assert(!cmd_dcc(4, close_all));
   event_emit("irc.disconnected", &alice, "");
   assert(!count_peers());
   /* Failed ranges must not publish a partial two-direction offer. */
   ws_conn = &alice;
   dict_add_int(cfg, "dcc.audio.port-min", 0);
   assert(cmd_dcc(3, audio) && !count_peers());
   assert(recv(sockets[1], wire, sizeof(wire), MSG_DONTWAIT) < 0);
   dict_add_int(cfg, "dcc.audio.port-min", 10000);
   int reserved = socket(AF_INET, SOCK_DGRAM, 0);
   assert(reserved >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(reserved, (struct sockaddr *)&address, sizeof(address)));
   socklen_t size = sizeof(address);
   assert(!getsockname(reserved, (struct sockaddr *)&address, &size));
   unsigned only_port = ntohs(address.sin_port);
   close(reserved);
   dict_add_int(cfg, "dcc.audio.port-min", only_port);
   dict_add_int(cfg, "dcc.audio.port-max", only_port);
   assert(cmd_dcc(3, audio) && !count_peers());
   assert(recv(sockets[1], wire, sizeof(wire), MSG_DONTWAIT) < 0);
   char *single[] = {
      "dcc", "audio", "bob", "rx"
   };
   assert(!cmd_dcc(4, single));
   assert(count_peers() == 1 && !peers[0].local_tx);
   peers[0].pending_until = mono_us() - 1;
   rrclient_dcc_poll();
   assert(!count_peers());
   assert(!cmd_dcc(4, single) && count_peers() == 1);
   alice.authenticated = false;
   rrclient_dcc_poll();
   assert(!count_peers());
   alice.authenticated = true;
   char *bad[] = {
      "dcc", "audio", "bob\r\nQUIT"
   };
   assert(cmd_dcc(3, bad));
   irc_shutdown();
   event_shutdown();
   dict_free(cfg);
   close(sockets[0]);
   close(sockets[1]);
   puts("PASS: peer DCC RX-before-TX offers, private keys, configured ports, explicit acceptance, microphone transport, mute and cleanup");
}

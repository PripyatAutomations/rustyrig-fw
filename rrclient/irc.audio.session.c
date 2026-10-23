//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrclient/irc.dcc.h>
/* Application-owned DCC AUDIO media sessions; librustyirc only decodes and emits offers. */
#include <rrclient/irc.audio.session.h>
#include <rrclient/irc.h>
#include <librrprotocol/rtp.socket.h>
#include <librrprotocol/ws.binframe.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HOST_POSIX
#include <sys/random.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define MAX_PEERS IRC_AUDIO_MAX_SESSIONS
struct peer_audio {
   rrconn_t *control;
   char nick[NICKLEN + 1];
   dict *offer;
   rr_rtp_socket_t *socket;
   rr_rtp_attachment_t *attachment;
   uint64_t pending_until;
   uint32_t sequence;
   bool local_tx, outgoing;
};
static struct peer_audio peers[MAX_PEERS];
static struct peer_audio *free_peer(void) {
   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      if (!peers[i].control) {
         return &peers[i];
      }
   }

   return NULL;
}
static void release(struct peer_audio *peer) {
   rrconn_t *control = peer->control;
   bool notify = peer->attachment && !peer->local_tx;
   char id[64];
   snprintf(id, sizeof(id), "%s", dict_get(peer->offer, "media.chan-uuid", ""));
   rr_rtp_socket_close(peer->socket);
   dict_free(peer->offer);
   memset(peer, 0, sizeof(*peer));

   if (notify) {
      event_emit(IRC_AUDIO_CLOSED_EVENT, control, id);
   }
}
static bool nick_valid(const char *nick) {
   if (!nick || !*nick || strlen(nick) > NICKLEN) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)nick ; *p ; p++) {
      if (*p <= ' ' || *p >= 127 || strchr(",:#!&", *p)) {
         return false;
      }
   }

   return true;
}
static rr_rtp_socket_t *listener(const irc_audio_options_t *options, unsigned *port) {
   unsigned first = options->port_min, last = options->port_max;

   if (first < 1 || last > 65535 || last < first) {
      return NULL;
   }
   uint32_t random;

   if (getrandom(&random, sizeof(random), 0) != sizeof(random)) {
      return NULL;
   }
   unsigned count = last - first + 1, start = random % count;

   for (unsigned i = 0 ; i < count ; i++) {
      *port = first + (start + i) % count;
      rr_rtp_socket_t *socket = rr_rtp_socket_open(options->bind ? options->bind : "0.0.0.0", *port);

      if (socket) {
         return socket;
      }
   }

   return NULL;
}
static bool decode_key(dict *offer, uint8_t key[RR_RTP_KEY_LEN]) {
   const char *text = dict_get(offer, "media.direct-key", "");

   if (strlen(text) != RR_RTP_KEY_LEN * 2) {
      return false;
   }

   for (unsigned i = 0 ; i < RR_RTP_KEY_LEN ; i++) {
      unsigned value;

      if (sscanf(text + 2 * i, "%2x", &value) != 1) {
         return false;
      }
      key[i] = value;
   }

   return true;
}
static bool attach(struct peer_audio *peer, const char *endpoint) {
   uint8_t key[RR_RTP_KEY_LEN];

   if (!decode_key(peer->offer, key)) {
      return false;
   }
   bool encrypt = !strcmp(dict_get(peer->offer, "media.direct-security", ""), "aes128cm-sha1-80");
   peer->attachment = rr_rtp_socket_attach(peer->socket, dict_get_uint(peer->offer, "media.direct-ssrc", 0), key, encrypt, endpoint, peer);
   memset(key, 0, sizeof(key));

   return peer->attachment != NULL;
}
static bool prepare_offer(rrconn_t *client, const char *nick, bool tx, const irc_audio_options_t *options, struct peer_audio **created) {
   struct peer_audio *peer = free_peer();
   char ip[64];

   if (!peer || !irc_dcc_advertised_ip(client, options->host, ip)) {
      return false;
   }
   unsigned port;
   peer->socket = listener(options, &port);

   if (!peer->socket) {
      return false;
   }
   uint8_t key[RR_RTP_KEY_LEN], identity[16];
   uint32_t ssrc;

   if (getrandom(key, sizeof(key), 0) != sizeof(key) || getrandom(identity, sizeof(identity), 0) != sizeof(identity) ||
      getrandom(&ssrc, sizeof(ssrc), 0) != sizeof(ssrc)) {
      release(peer);

      return false;
   }
   char hex[61], id[33], endpoint[96];

   for (unsigned i = 0 ; i < sizeof(key) ; i++) {
      snprintf(hex + i * 2, 3, "%02x", key[i]);
   }

   for (unsigned i = 0 ; i < sizeof(identity) ; i++) {
      snprintf(id + i * 2, 3, "%02x", identity[i]);
   }

   memset(key, 0, sizeof(key));
   peer->offer = dict_new();

   if (!peer->offer) {
      release(peer);

      return false;
   }
   snprintf(endpoint, sizeof(endpoint), "rtp://%s:%u", ip, port);
   dict_add(peer->offer, "media.kind", "voice");
   dict_add(peer->offer, "media.codec", "s16l");
   dict_add(peer->offer, "media.direct", endpoint);
   dict_add(peer->offer, "media.direct-key", hex);
   dict_add(peer->offer, "media.parent-session", id);
   dict_add(peer->offer, "media.chan-uuid", id);
   dict_add_uint(peer->offer, "media.direct-ssrc", (ssrc & 0x7fffffffU) ? (ssrc & 0x7fffffffU) : 1);
   dict_add_uint(peer->offer, "media.stream", 1);
   dict_add_uint(peer->offer, "media.owner-stream", 1);
   dict_add_uint(peer->offer, "media.dir", tx ? 1 : 0);
   dict_add(peer->offer, "media.direct-security", options->encrypt ? "aes128cm-sha1-80" : "null-sha1-80");
   peer->control = client;
   peer->outgoing = true;
   peer->local_tx = tx;
   snprintf(peer->nick, sizeof(peer->nick), "%s", nick);
   peer->pending_until = mono_us() + UINT64_C(300000000);

   if (!attach(peer, NULL)) {
      release(peer);

      return false;
   }
   *created = peer;

   return true;
}
static void offered(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   dict *offer = data ? json2dict(data) : NULL;

   if (!offer) {
      return;
   }

   if (!client || client->is_ws || !client->authenticated || strcmp(dict_get(offer, "media.kind", ""), "voice")) {
      dict_free(offer);

      return;
   }
   const char *prefix = dict_get(offer, "irc.prefix", "");
   char nick[NICKLEN + 1];
   size_t n = strcspn(prefix, "!");

   if (!n || n > NICKLEN) {
      dict_free(offer);

      return;
   }
   memcpy(nick, prefix, n);
   nick[n] = 0;

   if (!nick_valid(nick)) {
      dict_free(offer);

      return;
   }

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      if (peers[i].control == client && !strcasecmp(peers[i].nick, nick) &&
         !strcmp(dict_get(peers[i].offer, "media.chan-uuid", ""), dict_get(offer, "media.chan-uuid", ""))) {
         dict_free(offer);

         return;
      }
   }

   struct peer_audio *peer = free_peer();

   if (!peer) {
      dict_free(offer);

      return;
   }
   peer->control = client;
   peer->offer = offer;
   peer->local_tx = dict_get_uint(offer, "media.dir", 2) == 0;
   snprintf(peer->nick, sizeof(peer->nick), "%s", nick);
   peer->pending_until = mono_us() + UINT64_C(300000000);
   dict *notice = dict_new();

   if (notice) {
      dict_add(notice, "dcc.nick", nick);
      dict_add_bool(notice, "dcc.tx", peer->local_tx);
      dict_add(notice, "dcc.attachment", dict_get(offer, "media.chan-uuid", ""));
      event_emit_dict(IRC_AUDIO_OFFER_EVENT, client, notice);
      dict_free(notice);
   }
}
static bool receive(void *context, const uint8_t *data, size_t length) {
   struct peer_audio *peer = context;

   if (!data) {
      return true;
   }
   struct rr_binframe frame;

   if (peer->local_tx || rr_binframe_parse(data, length, &frame) || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
      frame.hdr.stream != 1 || frame.hdr.direction != dict_get_uint(peer->offer, "media.dir", 2) ||
      memcmp(frame.hdr.codec, "s16l", 4) || !frame.len || frame.len > 640 || frame.len % 2) {
      return false;
   }
   irc_audio_pcm_t pcm = {
      .attachment = dict_get(peer->offer, "media.chan-uuid", ""), .samples = frame.data, .length = frame.len
   };
   event_emit_binary(IRC_AUDIO_PCM_EVENT, peer->control, &pcm, sizeof(pcm));

   return true;
}
void irc_audio_sessions_poll(void) {
   uint64_t now = mono_us();

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      struct peer_audio *peer = &peers[i];

      if (!peer->control) {
         continue;
      }

      if (!peer->control->authenticated || (peer->pending_until && now >= peer->pending_until)) {
         release(peer);
         continue;
      }
      rr_rtp_socket_poll(peer->socket, now, NULL, receive);

      if (rr_rtp_socket_active(peer->attachment, now)) {
         peer->pending_until = 0;
      }
   }
}
void irc_audio_send_pcm(rrconn_t *control, const void *pcm, size_t length) {
   if (!pcm || length % 2) {
      return;
   }
   uint64_t now = mono_us();

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      struct peer_audio *peer = &peers[i];

      if (!peer->control || (control && peer->control != control) || !peer->control->authenticated || !peer->local_tx
         || !rr_rtp_socket_active(peer->attachment, now)) {
         continue;
      }

      for (size_t offset = 0 ; offset < length ; offset += 640) {
         size_t amount = length - offset < 640 ? length - offset : 640;
         uint8_t *frame = NULL;
         int bytes =
            rr_binframe_frame(&frame, RR_BINFRAME_SUBSYS_AUDIO, "s16l", dict_get_uint(peer->offer, "media.dir", 2), 255, 0, 1, peer->sequence++, now, (const
               uint8_t *)pcm + offset, amount);

         if (bytes > 0) {
            rr_rtp_socket_send(peer->attachment, frame, bytes, now);
         }
         free(frame);
      }
   }
}
static void closed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)user;

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      if (peers[i].control == client) {
         release(&peers[i]);
      }
   }
}

static bool connected(rrconn_t *client) {
   return client && !client->is_ws && client->authenticated;
}
bool irc_audio_offer(rrconn_t *client, const char *nick, unsigned directions, const irc_audio_options_t *options) {
   if (!connected(client) || !nick_valid(nick) || !options || !directions || (directions & ~IRC_AUDIO_BOTH) || !rr_rtp_supported(options->encrypt)) {
      return false;
   }
   struct peer_audio *first = NULL, *second = NULL;

   if (((directions & IRC_AUDIO_RX) && !prepare_offer(client, nick, false, options, &first)) ||
      ((directions & IRC_AUDIO_TX) && !prepare_offer(client, nick, true, options, &second))) {
      if (first) {
         release(first);
      }

      if (second) {
         release(second);
      }

      return false;
   }
   struct peer_audio *offers[2] = {
      first, second
   };

   for (unsigned i = 0 ; i < 2 ; i++) {
      struct peer_audio *peer = offers[i];

      if (!peer) {
         continue;
      }

      if (!irc_send_dcc_audio(client, nick, dict_get(peer->offer, "media.parent-session", ""), peer->offer)) {
         if (first) {
            release(first);
         }

         if (second) {
            release(second);
         }

         return false;
      }
   }

   return true;
}
bool irc_audio_accept(rrconn_t *client, const char *nick, unsigned directions) {
   if (!connected(client) || !nick_valid(nick) || !directions || (directions & ~IRC_AUDIO_BOTH)) {
      return false;
   }
   bool found = false;

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      struct peer_audio *peer = &peers[i];

      if (peer->control != client || strcasecmp(peer->nick, nick) || peer->outgoing || peer->attachment ||
         !(directions & (peer->local_tx ? IRC_AUDIO_TX : IRC_AUDIO_RX)) || mono_us() >= peer->pending_until) {
         continue;
      }
      peer->socket = rr_rtp_socket_open(NULL, 0);

      if (!peer->socket || !attach(peer, dict_get(peer->offer, "media.direct", ""))) {
         rr_rtp_socket_close(peer->socket);
         peer->socket = NULL;

         return false;
      }
      found = true;
   }

   return found;
}
unsigned irc_audio_close(rrconn_t *client, const char *nick, unsigned directions) {
   unsigned count = 0;

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      struct peer_audio *peer = &peers[i];

      if (!peer->control || peer->control != client || (nick && strcasecmp(peer->nick, nick)) || !(directions & (peer->local_tx ? IRC_AUDIO_TX : IRC_AUDIO_RX)))
      {
         continue;
      }
      release(peer);
      count++;
   }

   return count;
}
bool irc_audio_session_get(unsigned index, irc_audio_session_t *session) {
   if (!session) {
      return false;
   }

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      struct peer_audio *peer = &peers[i];

      if (!peer->control) {
         continue;
      }

      if (index--) {
         continue;
      }
      memset(session, 0, sizeof(*session));
      session->control = peer->control;
      session->tx = peer->local_tx;
      session->outgoing = peer->outgoing;
      session->accepted = peer->attachment != NULL;
      session->active = rr_rtp_socket_active(peer->attachment, mono_us());
      snprintf(session->nick, sizeof(session->nick), "%s", peer->nick);
      snprintf(session->endpoint, sizeof(session->endpoint), "%s", dict_get(peer->offer, "media.direct", ""));

      return true;
   }

   return false;
}
void irc_audio_sessions_shutdown(void) {
   event_off("irc.dcc.audio", offered, NULL);
   event_off("irc.disconnected", closed, NULL);
   event_off("disconnected", closed, NULL);
   event_off("disconnect", closed, NULL);

   for (unsigned i = 0 ; i < MAX_PEERS ; i++) {
      release(&peers[i]);
   }
}
void irc_audio_sessions_init(void) {
   event_off("irc.dcc.audio", offered, NULL);
   event_off("irc.disconnected", closed, NULL);
   event_off("disconnected", closed, NULL);
   event_off("disconnect", closed, NULL);
   event_on("irc.dcc.audio", offered, NULL);
   event_on("irc.disconnected", closed, NULL);
   event_on("disconnected", closed, NULL);
   event_on("disconnect", closed, NULL);
}
#else
bool irc_audio_offer(rrconn_t *client, const char *nick, unsigned directions, const irc_audio_options_t *options) {
   (void)client;
   (void)nick;
   (void)directions;
   (void)options;

   return false;
}
bool irc_audio_accept(rrconn_t *client, const char *nick, unsigned directions) {
   (void)client;
   (void)nick;
   (void)directions;

   return false;
}
unsigned irc_audio_close(rrconn_t *client, const char *nick, unsigned directions) {
   (void)client;
   (void)nick;
   (void)directions;

   return 0;
}
bool irc_audio_session_get(unsigned index, irc_audio_session_t *session) {
   (void)index;
   (void)session;

   return false;
}
void irc_audio_sessions_shutdown(void) {
}
void irc_audio_sessions_init(void) {
}
void irc_audio_sessions_poll(void) {
}
void irc_audio_send_pcm(rrconn_t *control, const void *pcm, size_t length) {
   (void)control;
   (void)pcm;
   (void)length;
}
#endif

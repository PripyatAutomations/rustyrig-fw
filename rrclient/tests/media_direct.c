/* Exercise the native policy through public control and media events over real UDP. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/rtp.h>
#include <rrclient/irc.h>
#include <rrclient/media.direct.h>
static uint64_t time_offset;
long long mono_us(void) {
   struct timespec t;
   assert(!clock_gettime(CLOCK_MONOTONIC, &t));

   return (uint64_t) t.tv_sec * 1000000 + t.tv_nsec / 1000 + time_offset;
}

static int fd;
static struct sockaddr_in return_address;
static unsigned received_audio, errors;
static rrconn_t client;
bool cfg_get_bool(const char *key, bool fallback) {
   return dict_get_bool(cfg, key, fallback);
}
const char *rrclient_connection_name(const rrconn_t *connection) {
   return connection == &client ? "test" : "other";
}
void rrclient_connection_dispatch(rrconn_t *connection, void (*callback) (void *), void *data) {
   assert(connection == &client);
   callback(data);
}
void Log(logpriority_t priority, const char *subsystem, const char *format, ...) {
   (void)priority;
   (void)subsystem;
   (void)format;
}
static bool send_packet(const uint8_t *data, size_t length, void *user) {
   (void)user;

   return sendto(fd, data, length, 0, (struct sockaddr *)&return_address, sizeof(return_address)) == (ssize_t) length;
}
static void audio(const char *event, const void *data, size_t length, rrconn_t *connection, void *user) {
   (void)event;
   (void)user;
   struct rr_binframe frame;
   assert(connection == &client && !rr_binframe_parse(data, length, &frame));
   assert(frame.hdr.stream == 128 && frame.len == 640 && frame.data[0] == 7);
   received_audio++;
}
static void error(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)user;
   assert(connection == &client);
   dict *d = json2dict(data);
   assert(d && !strcmp(dict_get(d, "error.from", ""), "media.direct"));
   errors++;
   dict_free(d);
}

static unsigned route_notices;
static char fallback_from[32], fallback_to[32];
static void route_notice(const char *event, const char *data, rrconn_t *connection, void *user) {
   assert(connection == &client);
   dict *d = json2dict(data);
   assert(d && *dict_get(d, "media.transport", "") && *dict_get(d, "media.chan-uuid", ""));
   const char *transport = dict_get(d, "media.transport", "");
   assert(!strcmp(transport, "direct-rtp") || !strcmp(transport, "proxy-rtp") || !strcmp(transport, "proxy-media"));

   if (!strcmp(dict_get(d, "media.status", ""), "connecting")) {
      assert(strstr(dict_get(d, "media.endpoint", ""), "127.0.0.1:"));
   }

   if (*dict_get(d, "media.fallback", "")) {
      snprintf(fallback_from, sizeof(fallback_from), "%s", transport);
      snprintf(fallback_to, sizeof(fallback_to), "%s", dict_get(d, "media.fallback", ""));
      assert(!strstr(dict_get(d, "media.status", ""), "falling back"));
   }
   route_notices++;
   dict_free(d);
}

static unsigned requests;
static char transport[16], requested_uuid[64];
static void request_sent(const char *event, const void *data, size_t length, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;
   assert(length == sizeof(rr_server_io_t));
   dict *d = ((const rr_server_io_t *)data)->message;
   requests++;
   snprintf(transport, sizeof(transport), "%s", dict_get(d, "media.transport", ""));
   snprintf(requested_uuid, sizeof(requested_uuid), "%s", dict_get(d, "media.chan-uuid", ""));
}
static dict *message(const char *cmd, const char *uuid) {
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", cmd);
   dict_add(d, "media.chan-uuid", uuid);

   return d;
}
int main(void) {
   bool encrypted = getenv("RR_RTP_TEST_ENCRYPTED") != NULL;

   if (!rr_rtp_supported(encrypted)) {
      puts("SKIP: native direct media is not built");

      return 0;
   }
   cfg = dict_new();
   assert(cfg);
   assert(rrclient_direct_preferred("test"));
   dict_add_bool(cfg, "media.direct.prefer", false);
   assert(!rrclient_direct_preferred("test"));
   assert(rrclient_direct_set_preference(&client, true));
   assert(rrclient_direct_preferred("test") && !rrclient_direct_preferred("other"));
   fd = socket(AF_INET, SOCK_DGRAM, 0);
   assert(fd >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(fd, (struct sockaddr *)&address, sizeof(address)));
   socklen_t size = sizeof(address);
   assert(!getsockname(fd, (struct sockaddr *)&address, &size));
   struct timeval timeout = {
      .tv_sec = 1
   };
   setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
   char endpoint[64];
   snprintf(endpoint, sizeof(endpoint), "rtp://127.0.0.1:%u", ntohs(address.sin_port));
   uint8_t key[RR_RTP_KEY_LEN] = {
      0
   };
   char hex[61];
   memset(hex, '0', 60);
   hex[60] = 0;
   rr_rtp_t *rx = rr_rtp_new(key, 10, true, encrypted), *tx = rr_rtp_new(key, 20, true, encrypted);
   assert(rx && tx);
   client.authenticated = true;
   strcpy(client.token, "parent-session");
   event_init();
   event_on("client.media.transport", route_notice, NULL);
   event_on("error", error, NULL);
   rrclient_direct_register_events();
   event_on_binary(RR_AUDIO_FRAME_EVENT, audio, NULL);

   for (unsigned i = 0 ; i < 2 ; i++) {
      const char *uuid = i ? "tx-channel" : "rx-channel";
      dict *d = message("available", uuid);
      dict_add(d, "media.room", "#remote-rig0");
      dict_add(d, "media.direct", endpoint);
      rrclient_direct_message(&client, d);
      dict_free(d);
      /* Explicit modes must work even before discovery supplies an endpoint. */
      assert(rrclient_direct_set_mode(&client, uuid, RR_MEDIA_DIRECT));
      d = message("subscribe", uuid);
      rr_server_io_t strict = {
         .message = d
      };
      event_emit_binary(RR_MEDIA_REQUEST_EVENT, &client, &strict, sizeof(strict));
      assert(!strcmp(dict_get(d, "media.transport", ""), "direct"));
      dict_free(d);
      assert(rrclient_direct_set_mode(&client, uuid, RR_MEDIA_PROXY));
      d = message("subscribe", uuid);
      strict.message = d;
      event_emit_binary(RR_MEDIA_REQUEST_EVENT, &client, &strict, sizeof(strict));
      assert(!strcmp(dict_get(d, "media.transport", ""), "ws"));
      dict_free(d);
      assert(rrclient_direct_set_mode(&client, uuid, RR_MEDIA_AUTO));
      d = message("subscribe", uuid);
      rr_server_io_t request = {
         .message = d
      };
      event_emit_binary(RR_MEDIA_REQUEST_EVENT, &client, &request, sizeof(request));
      assert(!strcmp(dict_get(d, "media.transport", ""), "rtp"));
      assert(!strcmp(dict_get(d, "media.room", ""), "#remote-rig0"));
      dict_free(d);
      d = message("subscribed", uuid);
      dict_add_ulong(d, "media.stream", 128 + i);
      dict_add_ulong(d, "media.owner-stream", 1 + i);
      dict_add_ulong(d, "media.dir", i);
      dict_add(d, "media.direct", endpoint);
      dict_add(d, "media.direct-key", hex);
      dict_add_ulong(d, "media.direct-ssrc", i ? 20 : 10);
      dict_add(d, "media.direct-security", encrypted ? "aes128cm-sha1-80" : "null-sha1-80");

      if (!i) {
         /* A peer voice offer cannot use the owner-rig automatic exception. */
         dict_add(d, "media.kind", "voice");
         dict_add(d, "media.codec", "s16l");
         char *voice_payload = irc_dcc_audio_encode(client.token, d);
         assert(voice_payload);
         dict *voice_offer = irc_dcc_audio_decode(voice_payload);
         assert(voice_offer);
         free(voice_payload);
         event_emit_dict("irc.dcc.audio", &client, voice_offer);
         dict_free(voice_offer);
         rrclient_direct_poll();
         uint8_t no_peer[1200];
         assert(recv(fd, no_peer, sizeof(no_peer), MSG_DONTWAIT) < 0);
         dict_del(d, "media.kind");
         dict_del(d, "media.codec");
         char *payload = irc_dcc_audio_encode("wrong-session", d);
         assert(payload);
         dict *offer = irc_dcc_audio_decode(payload);
         assert(offer);
         free(payload);
         event_emit_dict("irc.dcc.audio", &client, offer);
         dict_free(offer);
         rrclient_direct_poll();
         uint8_t ignored[1200];
         assert(recv(fd, ignored, sizeof(ignored), MSG_DONTWAIT) < 0);
         payload = irc_dcc_audio_encode(client.token, d);
         assert(payload);
         offer = irc_dcc_audio_decode(payload);
         assert(offer);
         free(payload);
         event_emit_dict("irc.dcc.audio", &client, offer);
         /* A discovered channel without a subscription must not attach unsolicited offers. */
         dict_add(offer, "media.chan-uuid", "unsolicited-channel");
         assert(rrclient_direct_set_mode(&client, "unsolicited-channel", RR_MEDIA_AUTO));
         event_emit_dict("irc.dcc.audio", &client, offer);
         dict_free(offer);
      } else {
         rrclient_direct_message(&client, d);
      }
      dict_free(d);
   }

   /* Forced TX must not fall through to WS while the UDP handshake is pending. */
   assert(rrclient_direct_set_mode(&client, "tx-channel", RR_MEDIA_DIRECT));
   uint8_t pending_payload[2] = {
      0
   }, *pending_wire;
   int pending_length = rr_binframe_frame(&pending_wire, 1, "pc16", 1, 255, 0, 129, 1, 0, pending_payload, 2);
   rr_media_binary_t pending = {
      .frame = pending_wire, .length = pending_length
   };
   event_emit_binary(RR_MEDIA_BINARY_EVENT, &client, &pending, sizeof(pending));
   assert(pending.handled && !pending.result);
   free(pending_wire);
   rrclient_direct_poll();

   for (unsigned i = 0 ; i < 2 ; i++) {
      uint8_t packet[1200];
      size = sizeof(return_address);
      ssize_t count = recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&return_address, &size);
      assert(count > 0);
      const uint8_t *frame;
      size_t length;
      rr_rtp_t *peer = packet[7] == 10 ? rx : tx;
      assert(rr_rtp_receive(peer, packet, count, mono_us(), &frame, &length) == 2);
      assert(rr_rtp_probe(peer, send_packet, NULL));
   }

   uint8_t unsolicited[1200];
   assert(recv(fd, unsolicited, sizeof(unsolicited), MSG_DONTWAIT) < 0);
   rrclient_direct_poll();
   uint8_t payload[640];
   memset(payload, 7, sizeof(payload));
   uint8_t *wire;
   int length = rr_binframe_frame(&wire, 1, "pc16", 0, 255, 0, 1, 1, 0, payload, sizeof(payload));
   assert(length > 0 && rr_rtp_send(rx, wire, length, mono_us(), send_packet, NULL));
   free(wire);
   rrclient_direct_poll();
   assert(received_audio == 1);
   length = rr_binframe_frame(&wire, 1, "pc16", 1, 255, 0, 129, 1, 0, payload, sizeof(payload));
   rr_media_binary_t outgoing = {
      .frame = wire, .length = length
   };
   event_emit_binary(RR_MEDIA_BINARY_EVENT, &client, &outgoing, sizeof(outgoing));
   assert(outgoing.handled && outgoing.result);
   uint8_t packet[1200];
   ssize_t count = recv(fd, packet, sizeof(packet), 0);
   const uint8_t *frame;
   size_t frame_length;
   assert(count > 0 && rr_rtp_receive(tx, packet, count, mono_us(), &frame, &frame_length) == 1);
   struct rr_binframe parsed;
   assert(!rr_binframe_parse(frame, frame_length, &parsed) && parsed.hdr.stream == 2 && parsed.len == 640);
   /* Codec setup remains on the reliable control path even while UDP is healthy. */
   outgoing = (rr_media_binary_t) {
      .frame = wire, .length = length, .setup = true
   };
   event_emit_binary(RR_MEDIA_BINARY_EVENT, &client, &outgoing, sizeof(outgoing));
   assert(!outgoing.handled);
   event_on_binary(RR_MEDIA_REQUEST_EVENT, request_sent, NULL);
   assert(rrclient_direct_set_preference(&client, false));
   assert(requests == 1 && !strcmp(transport, "ws") && !strcmp(requested_uuid, "rx-channel"));
   /* The explicit TX stream retains direct transport across the preference switch. */
   outgoing = (rr_media_binary_t) {
      .frame = wire, .length = length
   };
   event_emit_binary(RR_MEDIA_BINARY_EVENT, &client, &outgoing, sizeof(outgoing));
   assert(outgoing.handled && outgoing.result);
   assert(rrclient_direct_set_preference(&client, true));
   assert(requests == 2 && !strcmp(transport, "rtp") && !strcmp(requested_uuid, "rx-channel"));
   event_emit("disconnected", &client, NULL);
   outgoing = (rr_media_binary_t) {
      .frame = wire, .length = length
   };
   event_emit_binary(RR_MEDIA_BINARY_EVENT, &client, &outgoing, sizeof(outgoing));
   assert(!outgoing.handled);
   assert(rrclient_direct_set_mode(&client, "missing", RR_MEDIA_DIRECT));
   dict *missing = message("subscribed", "missing");
   rrclient_direct_message(&client, missing);
   dict_free(missing);
   assert(errors == 1);
   event_emit("disconnected", &client, NULL);
   /* A silent owner advances to home RTP, then proxy; no referral does the same. */
   assert(rrclient_direct_set_mode(&client, "fallback", RR_MEDIA_AUTO));
   dict *retry = message("subscribe", "fallback");
   rr_server_io_t retry_io = {
      .message = retry
   };
   event_emit_binary(RR_MEDIA_REQUEST_EVENT, &client, &retry_io, sizeof(retry_io));
   assert(!strcmp(transport, "rtp"));
   dict_free(retry);
   retry = message("subscribed", "fallback");
   dict_add_ulong(retry, "media.stream", 130);
   dict_add_ulong(retry, "media.owner-stream", 3);
   dict_add(retry, "media.direct", endpoint);
   dict_add(retry, "media.direct-key", hex);
   dict_add_ulong(retry, "media.direct-ssrc", 30);
   dict_add(retry, "media.direct-security", encrypted ? "aes128cm-sha1-80" : "null-sha1-80");
   rrclient_direct_message(&client, retry);
   time_offset += 5000001;
   rrclient_direct_poll();
   assert(!strcmp(transport, "rtp-home"));
   assert(!strcmp(fallback_from, "direct-rtp") && !strcmp(fallback_to, "proxy-rtp"));
   dict_add_ulong(retry, "media.owner-stream", 130);
   rrclient_direct_message(&client, retry);
   time_offset += 5000001;
   rrclient_direct_poll();
   assert(!strcmp(transport, "ws"));
   assert(!strcmp(fallback_from, "proxy-rtp") && !strcmp(fallback_to, "proxy-media"));
   dict_free(retry);
   event_emit("disconnected", &client, NULL);
   assert(rrclient_direct_set_mode(&client, "unavailable", RR_MEDIA_AUTO));
   retry = message("subscribed", "unavailable");
   dict_add_ulong(retry, "media.stream", 130);
   dict_add_ulong(retry, "media.owner-stream", 3);
   rrclient_direct_message(&client, retry);
   assert(!strcmp(transport, "rtp-home"));
   assert(!strcmp(fallback_from, "direct-rtp") && !strcmp(fallback_to, "proxy-rtp"));
   rrclient_direct_message(&client, retry);
   assert(!strcmp(transport, "ws"));
   dict_free(retry);
   assert(route_notices > 0);
   event_emit("disconnected", &client, NULL);
   free(wire);
   rr_rtp_free(rx);
   rr_rtp_free(tx);
   close(fd);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: native RTP preference, DCC parent-session association, private RX/TX grants, stream translation, reliable setup and disconnect fallback");
}

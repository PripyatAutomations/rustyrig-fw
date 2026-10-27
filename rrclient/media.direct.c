//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Native RTP policy: client-originated UDP, authenticated control grants, WS fallback. */
#include "build_config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <rrclient/media.direct.h>
#include <rrclient/connman.h>
bool rrclient_direct_preferred(const char *profile) {
   bool fallback = cfg_get_bool("media.direct.prefer", true);

   if (!profile || !*profile) {
      return fallback;
   }
   char key[640];
   snprintf(key, sizeof(key), "server:%s.media.direct.prefer", profile);

   return cfg_get_bool(key, fallback);
}
bool rrclient_direct_set_preference(rrconn_t *client, bool enabled) {
   const char *profile = rrclient_connection_name(client);

   if (!profile) {
      return false;
   }
   char key[640];
   snprintf(key, sizeof(key), "server:%s.media.direct.prefer", profile);

   if (!cfg || dict_add(cfg, key, enabled ? "true" : "false")) {
      return false;
   }
   rrclient_direct_apply_preferences();

   return true;
}

#ifdef USE_MONGOOSE
static bool preferred(rrconn_t *client) {
   return rrclient_direct_preferred(rrclient_connection_name(client));
}

#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/rtp.socket.h>
#include <librrprotocol/traffic.h>
#include <librrprotocol/ws.mediachan.h>
#define MAX_DIRECT_CHANNELS 128
struct direct_channel {
   rrconn_t *client;
   char uuid[64], room[128], endpoint[320], codec[5];
   rr_rtp_attachment_t *attachment;
   uint64_t renew_at, connect_deadline;
   char candidates[8][320];
   unsigned candidate_count, candidate_index;
   uint8_t grant_key[RR_RTP_KEY_LEN];
   uint32_t grant_ssrc;
   bool grant_encrypted;
   uint64_t rtp_rx_bytes, rtp_tx_bytes;
   rr_media_transport_t mode;
   bool failed, requested, preference, reported_active;
   unsigned route;              /* 0: owner RTP, 1: connected-server RTP, 2: WebSocket. */
   bool remote;
   uint8_t stream, owner_stream, direction;
   uint32_t subsystem;
};
static struct direct_channel channels[MAX_DIRECT_CHANNELS];
static rr_rtp_socket_t *media_socket;
static struct direct_channel *find(rrconn_t *client, const char *uuid, bool create) {
   struct direct_channel *empty = NULL;

   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; i++) {
      if (channels[i].client == client && !strcmp(channels[i].uuid, uuid)) {
         return &channels[i];
      }

      if (!channels[i].client && !empty) {
         empty = &channels[i];
      }
   }

   if (!create || !empty || !client || !*uuid || strlen(uuid) >= sizeof(empty->uuid)) {
      return NULL;
   }
   empty->client = client;
   empty->preference = preferred(client);
   snprintf(empty->uuid, sizeof(empty->uuid), "%s", uuid);

   return empty;
}
bool rrclient_direct_set_mode(rrconn_t *client, const char *uuid, rr_media_transport_t mode) {
   if (mode == RR_MEDIA_DIRECT && !rr_rtp_supported(false)) {
      return false;
   }
   struct direct_channel *channel = find(client, uuid, true);

   if (!channel) {
      return false;
   }
   channel->mode = mode;
   channel->failed = false;
   channel->route = 0;

   return true;
}
static void report_route(struct direct_channel *channel, const char *status, bool rtp) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   char endpoint[384] = "";

   if (rtp) {
      snprintf(endpoint, sizeof(endpoint), "%s", channel->endpoint);
   } else if (channel->client->conn && channel->client->conn->rem.port) {
      mg_snprintf(endpoint, sizeof(endpoint), "%M", mg_print_ip_port, &channel->client->conn->rem);
   } else if (channel->client->server) {
      snprintf(endpoint, sizeof(endpoint), "%s:%d", channel->client->server->host, channel->client->server->port);
   }
   dict_add(d, "media.chan-uuid", channel->uuid);
   dict_add(d, "media.room", channel->room);
   dict_add(d, "media.codec", channel->codec);
   const char *fallback = strstr(status, "; falling back");

   if (fallback) {
      char reason[256];
      snprintf(reason, sizeof(reason), "%.*s", (int)(fallback - status), status);
      dict_add(d, "media.status", reason);
      dict_add(d, "media.fallback", channel->remote && channel->route == 0 ? "proxy-rtp" : "proxy-media");
   } else {
      dict_add(d, "media.status", status);
   }
   dict_add(d, "media.transport", rtp && channel->route == 0 ? "direct-rtp" : rtp ? "proxy-rtp" : "proxy-media");
   dict_add(d, "media.endpoint", endpoint);
   event_emit_dict("client.media.transport", channel->client, d);
   dict_free(d);
}
static void direct_failed(struct direct_channel *channel, const char *reason) {
   if (channel->failed) {
      return;
   }
   channel->failed = true;
   dict *d = dict_new();

   if (!d) {
      return;
   }
   char text[512];
   snprintf(text, sizeof(text), "Direct media for %s: %s", channel->uuid, reason);
   dict_add(d, "error.msg", text);
   dict_add(d, "error.from", "media.direct");
   dict_add(d, "error.target", channel->room);
   event_emit_dict("error", channel->client, d);
   dict_free(d);
}
static void next_route(struct direct_channel *channel) {
   rr_rtp_socket_detach(channel->attachment);
   channel->attachment = NULL;
   channel->renew_at = channel->connect_deadline = 0;

   if (!channel->remote || channel->route == 1) {
      channel->route = 2;
   } else {
      channel->route = 1;
   }
   media_send_subscribe(channel->client, channel->uuid);
}
static bool try_candidate(struct direct_channel *channel) {
   while (channel->candidate_index < channel->candidate_count) {
      const char *endpoint = channel->candidates[channel->candidate_index++];
      rr_rtp_socket_detach(channel->attachment);
      channel->attachment = rr_rtp_socket_attach(media_socket, channel->grant_ssrc, channel->grant_key, channel->grant_encrypted, endpoint, channel);

      if (!channel->attachment) {
         continue;
      }
      snprintf(channel->endpoint, sizeof(channel->endpoint), "%s", endpoint);
      channel->connect_deadline = mono_us() + UINT64_C(1500000);
      report_route(channel, "connecting", true);

      return true;
   }
   return false;
}
static void load_candidates(struct direct_channel *channel, const char *list, const char *single) {
   channel->candidate_count = channel->candidate_index = 0;

   if (!list || !*list) {
      list = single;
   }

   if (!list || !*list) {
      return;
   }
   char *copy = strdup(list);

   if (!copy) {
      return;
   }
   char *save = NULL;

   for (char *p = strtok_r(copy, ",", &save) ; p && channel->candidate_count < 8 ; p = strtok_r(NULL, ",", &save)) {
      if (strlen(p) >= sizeof(channel->candidates[0]) || strncmp(p, "rtp://", 6)) {
         continue;
      }
      snprintf(channel->candidates[channel->candidate_count++], sizeof(channel->candidates[0]), "%s", p);
   }

   free(copy);
}
/* Refresh before the advertised lease expires; fall back to the legacy
 * 240-second interval when talking to older servers. Bound untrusted
 * values so malformed grants cannot suppress refresh indefinitely. */
static uint64_t direct_renew_delay(dict *grant) {
   unsigned long lifetime = dict_get_ulong(grant, "media.direct-lease-seconds", 0);
   if (lifetime < 10 || lifetime > 86400) {
      return UINT64_C(240000000);
   }
   unsigned long margin = lifetime / 5;
   if (margin < 5) {
      margin = 5;
   }
   return (uint64_t)(lifetime - margin) * UINT64_C(1000000);
}
static bool hex_key(const char *text, uint8_t *key) {
   if (strlen(text) != RR_RTP_KEY_LEN * 2) {
      return false;
   }

   for (unsigned i = 0 ; i < RR_RTP_KEY_LEN ; i++) {
      unsigned a, b;
      char c = text[i * 2], d = text[i * 2 + 1];
      a = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99;
      b = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10 : 99;

      if (a > 15 || b > 15) {
         return false;
      }
      key[i] = (a << 4) | b;
   }

   return true;
}
void rrclient_direct_message(rrconn_t *client, dict *d) {
   const char *cmd = dict_get(d, "media.cmd", ""), *uuid = dict_get(d, "media.chan-uuid", "");
   struct direct_channel *channel = find(client, uuid, !strcmp(cmd, "available") || !strcmp(cmd, "subscribed"));

   if (!channel) {
      return;
   }

   const char *codec = dict_get(d, "media.codec", "");

   if (strlen(codec) == 4) {
      snprintf(channel->codec, sizeof(channel->codec), "%s", codec);
   }

   if (!strcmp(cmd, "available")) {
      channel->subsystem = dict_get_ulong(d, "media.subsys", 0);
      snprintf(channel->room, sizeof(channel->room), "%s", dict_get(d, "media.room", ""));
      snprintf(channel->endpoint, sizeof(channel->endpoint), "%s", dict_get(d, "media.direct", ""));
   } else if (!strcmp(cmd, "subscribed")) {
      channel->subsystem = dict_get_ulong(d, "media.subsys", channel->subsystem);
      channel->requested = true;
      channel->stream = dict_get_ulong(d, "media.stream", 0);
      channel->owner_stream = dict_get_ulong(d, "media.owner-stream", channel->stream);
      channel->direction = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_NA);
      channel->remote = channel->stream != dict_get_ulong(d, "media.owner-stream", channel->stream) || channel->remote;
      const char *security = dict_get(d, "media.direct-security", "");
      bool encrypted = !strcmp(security, "aes128cm-sha1-80");
      uint8_t key[RR_RTP_KEY_LEN];
      unsigned long ssrc = dict_get_ulong(d, "media.direct-ssrc", 0);
      const char *endpoint = dict_get(d, "media.direct", "");
      /* A periodic four-minute grant refresh is not a disconnect. Keep an
       * already-active RTP attachment if the server renewed the same grant. */
      bool unchanged_grant = channel->attachment &&
         rr_rtp_socket_active(channel->attachment, mono_us()) &&
         ssrc == channel->grant_ssrc &&
         encrypted == channel->grant_encrypted &&
         hex_key(dict_get(d, "media.direct-key", ""), key) &&
         memcmp(key, channel->grant_key, sizeof(key)) == 0 &&
         (!*endpoint || !strcmp(endpoint, channel->endpoint));
      load_candidates(channel, dict_get(d, "media.direct-candidates", ""), endpoint);
      if (unchanged_grant) {
         memset(key, 0, sizeof(key));
         channel->renew_at = mono_us() + direct_renew_delay(d);
         channel->connect_deadline = mono_us() + UINT64_C(5000000);
         return;
      }
      rr_rtp_socket_detach(channel->attachment);
      channel->attachment = NULL;
      channel->failed = false;
      channel->reported_active = false;
      channel->connect_deadline = mono_us() + UINT64_C(5000000);

      if (channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO && channel->subsystem != RR_BINFRAME_SUBSYS_VIDEO) {
         report_route(channel, "connected", false);

         return;
      }

      if (channel->mode == RR_MEDIA_PROXY || (channel->mode == RR_MEDIA_AUTO && channel->route == 2)) {
         report_route(channel, "connected", false);

         return;
      }

      if ((channel->mode == RR_MEDIA_AUTO && !preferred(client)) || !rr_rtp_supported(encrypted) ||
         (!encrypted && strcmp(security, "null-sha1-80")) || !ssrc || ssrc > UINT32_MAX || !channel->candidate_count
         || !hex_key(dict_get(d, "media.direct-key", ""), key)) {
         if (channel->mode == RR_MEDIA_DIRECT) {
            direct_failed(channel, "the owner did not provide a usable RTP referral");
         } else if (preferred(client)) {
            const char *reason = !*endpoint ? "unavailable: server supplied no RTP endpoint; falling back" :
               !rr_rtp_supported(encrypted) ? "unavailable: RTP security not supported by this build; falling back" :
               "unavailable: invalid RTP grant; falling back";
            report_route(channel, reason, true);
            next_route(channel);
         } else {
            report_route(channel, "connected", false);
         }

         return;
      }

      if (!media_socket) {
         media_socket = rr_rtp_socket_open(NULL, 0);
      }
      memcpy(channel->grant_key, key, sizeof(key));
      memset(key, 0, sizeof(key));
      channel->grant_ssrc = (uint32_t) ssrc;
      channel->grant_encrypted = encrypted;
      channel->renew_at = mono_us() + direct_renew_delay(d);

      if (!try_candidate(channel)) {
         if (channel->mode == RR_MEDIA_DIRECT) {
            direct_failed(channel, "unable to open the RTP endpoint");
         } else {
            next_route(channel);
         }
      }
   } else if (!strcmp(cmd, "unsubscribed") || !strcmp(cmd, "chan-remove")) {
      rr_rtp_socket_detach(channel->attachment);
      memset(channel, 0, sizeof(*channel));
   }
}

struct received_frame {
   rrconn_t *client;
   const char *data;
   size_t length;
   bool result;
};
static void dispatch_received(void *user) {
   struct received_frame *frame = user;
   frame->result = ws_binframe_process_client(frame->client, frame->data, frame->length);
}
static bool received(void *user, const uint8_t *data, size_t length) {
   struct direct_channel *channel = user;

   if (!data) {
      return true;
   }
   struct rr_binframe frame;

   if (!channel->client || channel->direction != RR_BINFRAME_DIR_RX || rr_binframe_parse(data, length, &frame) ||
      frame.hdr.stream != channel->owner_stream || frame.hdr.direction != RR_BINFRAME_DIR_RX || frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
      return false;
   }
   uint8_t *copy = malloc(length);

   if (!copy) {
      return false;
   }
   memcpy(copy, data, length);
   copy[11] = channel->stream;
   channel->rtp_rx_bytes += length;
   rr_traffic_count(channel->client, false, true, length);
   struct received_frame received = {
      channel->client, (const char *)copy, length, false
   };
   rrclient_connection_dispatch(channel->client, dispatch_received, &received);
   free(copy);

   return received.result;
}
void rrclient_direct_poll(void) {
   uint64_t now = mono_us();
   rr_rtp_socket_poll(media_socket, now, NULL, received);

   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; i++) {
      struct direct_channel *c = &channels[i];

      if (c->attachment && c->connect_deadline && now >= c->connect_deadline && !rr_rtp_socket_active(c->attachment, now)) {
         if (try_candidate(c)) {
            continue;
         }
      }

      if (c->mode == RR_MEDIA_AUTO && c->attachment && c->connect_deadline && now >= c->connect_deadline && !rr_rtp_socket_active(c->attachment, now)) {
         report_route(c, "timed out; falling back", true);
         next_route(c);
      }

      if (c->mode == RR_MEDIA_DIRECT && c->attachment && c->connect_deadline && now >= c->connect_deadline && !rr_rtp_socket_active(c->attachment, now)) {
         direct_failed(c, "the RTP endpoint did not answer; use -proxy to select WebSockets");
      }

      if (rr_rtp_socket_active(c->attachment, now)) {
         if (!c->reported_active) {
            report_route(c, "connected", true);
            c->reported_active = true;
         }
         c->connect_deadline = now + UINT64_C(5000000);
      }

      if (c->attachment && c->renew_at && now >= c->renew_at) {
         c->renew_at = now + UINT64_C(240000000);
         media_send_subscribe(c->client, c->uuid);
      }
   }
}
bool rrclient_direct_traffic(rrconn_t *client, uint64_t *rx, uint64_t *tx) {
   uint64_t incoming = 0, outgoing = 0;
   bool active = false;

   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; ++i) {
      struct direct_channel *c = &channels[i];

      if (c->client != client) {
         continue;
      }
      incoming += c->rtp_rx_bytes;
      outgoing += c->rtp_tx_bytes;

      if (c->attachment && rr_rtp_socket_active(c->attachment, mono_us())) {
         active = true;
      }
   }

   if (rx) {
      *rx = incoming;
   }

   if (tx) {
      *tx = outgoing;
   }

   return active;
}
void rrclient_direct_apply_preferences(void) {
   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; i++) {
      struct direct_channel *c = &channels[i];

      if (!c->client) {
         continue;
      }
      bool enabled = preferred(c->client);

      if (c->preference == enabled) {
         continue;
      }
      c->preference = enabled;
      c->route = 0;

      if (!c->requested || c->mode != RR_MEDIA_AUTO) {
         continue;
      }

      if (!enabled) {
         rr_rtp_socket_detach(c->attachment);
         c->attachment = NULL;
         c->renew_at = 0;
      }
      media_send_subscribe(c->client, c->uuid);
   }
}
static bool preference_reload(const char *key) {
   (void)key;
   rrclient_direct_apply_preferences();

   return true;
}
static void request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (length != sizeof(rr_server_io_t)) {
      return;
   }
   dict *d = ((rr_server_io_t *) data)->message;
   struct direct_channel *c = find(client, dict_get(d, "media.chan-uuid", ""), false);

   if (!c) {
      return;
   }

   if (!strcmp(dict_get(d, "media.cmd", ""), "unsubscribe")) {
      c->requested = false;
      c->route = 0;
      c->remote = false;
      rr_rtp_socket_detach(c->attachment);
      c->attachment = NULL;
      c->renew_at = 0;
   }

   if (*c->room) {
      dict_add(d, "media.room", c->room);
   }

   if (!strcmp(dict_get(d, "media.cmd", ""), "subscribe")) {
      c->requested = true;

      if (c->subsystem != RR_BINFRAME_SUBSYS_AUDIO && c->subsystem != RR_BINFRAME_SUBSYS_VIDEO) {
         dict_add(d, "media.transport", "ws");
      } else if (c->mode == RR_MEDIA_DIRECT) {
         dict_add(d, "media.transport", "direct");
      } else if (c->mode == RR_MEDIA_PROXY) {
         dict_add(d, "media.transport", "ws");
      } else {
         dict_add(d, "media.transport", preferred(client) && rr_rtp_supported(false) && c->route < 2 ? (c->route == 1 ? "rtp-home" : "rtp") : "ws");
      }
      const char *transport = dict_get(d, "media.transport", "ws");
      report_route(c, "requesting", strcmp(transport, "ws") != 0);
   }
}
static void binary(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (length != sizeof(rr_media_binary_t)) {
      return;
   }
   rr_media_binary_t *io = (rr_media_binary_t *) data;
   struct rr_binframe frame;

   if (io->setup || rr_binframe_parse(io->frame, io->length, &frame) || frame.hdr.direction != RR_BINFRAME_DIR_TX) {
      return;
   }

   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; i++) {
      struct direct_channel *c = &channels[i];

      if (c->client != client || c->direction != RR_BINFRAME_DIR_TX || c->stream != frame.hdr.stream) {
         continue;
      }

      if (c->mode == RR_MEDIA_PROXY) {
         return;
      }

      if (io->length > RR_RTP_MAX_FRAME || !rr_rtp_socket_active(c->attachment, mono_us())) {
         if (c->mode == RR_MEDIA_DIRECT) {
            io->handled = true;
            io->result = false;
         }

         return;
      }
      uint8_t *copy = malloc(io->length);

      if (!copy) {
         if (c->mode == RR_MEDIA_DIRECT) {
            io->handled = true;
            io->result = false;
         }

         return;
      }
      memcpy(copy, io->frame, io->length);
      copy[11] = c->owner_stream;
      io->handled = true;
      io->result = rr_rtp_socket_send(c->attachment, copy, io->length, mono_us());
      free(copy);

      return;
   }
}
static void closed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)user;

   for (unsigned i = 0 ; i < MAX_DIRECT_CHANNELS ; i++) {
      if (channels[i].client == client) {
         rr_rtp_socket_detach(channels[i].attachment);
         memset(&channels[i], 0, sizeof(channels[i]));
      }
   }
}
static void irc_audio(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (!client || !client->authenticated || !*client->token || !data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   if (!strcmp(dict_get(d, "media.kind", ""), "voice")) {
      dict_free(d);

      return;
   }
   const char *parent = dict_get(d, "media.parent-session", "");
   struct direct_channel *channel = find(client, dict_get(d, "media.chan-uuid", ""), false);

   /* Never attach unsolicited offers, other sessions, or another server's channels. */
   if (channel && channel->requested && channel->mode != RR_MEDIA_PROXY && !strcmp(parent, client->token)) {
      rrclient_direct_message(client, d);
   }
   dict_free(d);
}
void rrclient_direct_register_events(void) {
   reload_event_add(NULL, preference_reload, "apply direct-media preferences to active automatic subscriptions");
   event_on("irc.dcc.audio", irc_audio, NULL);
   event_on_binary(RR_MEDIA_REQUEST_EVENT, request, NULL);
   event_on_binary(RR_MEDIA_BINARY_EVENT, binary, NULL);
   event_on("disconnected", closed, NULL);
   event_on("disconnect", closed, NULL);
}

#else
bool rrclient_direct_traffic(rrconn_t *client, uint64_t *rx, uint64_t *tx) {
   (void)client;

   if (rx) {
      *rx = 0;
   }

   if (tx) {
      *tx = 0;
   }

   return false;
}
void rrclient_direct_apply_preferences(void) {
}
bool rrclient_direct_set_mode(rrconn_t *client, const char *uuid, rr_media_transport_t mode) {
   (void)client;
   (void)uuid;

   return mode != RR_MEDIA_DIRECT;
}
void rrclient_direct_register_events(void) {
}
void rrclient_direct_message(rrconn_t *client, dict *message) {
   (void)client;
   (void)message;
}
void rrclient_direct_poll(void) {
}
#endif

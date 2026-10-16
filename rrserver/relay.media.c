//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Session-local media translation, fragmentation and proxy TX liveness. */
#include "build_config.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <rrserver/relay.h>
#include <rrserver/media.direct.h>
#include <rrserver/supervision.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/media.health.h>
#ifdef USE_MONGOOSE
#include <librrprotocol/server.events.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/wire.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <rrserver/objects.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/database.h>
#include <rrserver/discovery.h>
extern struct GlobalState rig;
#include <rrserver/relay.internal.h>
static uint32_t rr_relay_media_sequence;
static unsigned rr_relay_media_map_count;
struct relay_media *rr_relay_media_maps;

/* Find or allocate a session-local stream mapping within the relay limits. */
struct relay_media *rr_relay_media_map(rrconn_t *client, const char *uuid, bool create) {
   for (struct relay_media *m = rr_relay_media_maps ; m ; m = m->next) {
      if (m->client == client && !strcmp(m->uuid, uuid)) {
         return m;
      }
   }

   if (!create || rr_relay_media_map_count == MAX_RELAY_MEDIA || !*uuid || strlen(uuid) >= 64) {
      return NULL;
   }
   struct relay_media *m = calloc(1, sizeof(*m));

   if (!m) {
      return NULL;
   }
   m->client = client;
   snprintf(m->uuid, sizeof(m->uuid), "%s", uuid);

   for (unsigned stream = 128 ; stream <= UINT8_MAX ; stream++) {
      bool used = false;

      for (struct relay_media *p = rr_relay_media_maps ; p ; p = p->next) {
         if (p->client == client && p->stream == stream) {
            used = true;
         }
      }

      if (!used) {
         m->stream = stream;
         break;
      }
   }

   if (!m->stream) {
      free(m);

      return NULL;
   }
   m->next = rr_relay_media_maps;
   rr_relay_media_maps = m;
   rr_relay_media_map_count++;

   return m;
}

/* Check a subscribed home-side audio stream against its session and room. */
bool rrserver_relay_media_valid(rrconn_t *client, const char *uuid, unsigned stream) {
   struct relay_media *m = rr_relay_media_map(client, uuid, false);

   return m && m->subsystem == RR_BINFRAME_SUBSYS_AUDIO && m->home && m->subscribed && m->stream == stream && ws_client_in_room(client, m->room);
}

/* Remove matching mappings and notify subscribed clients before freeing them. */
void rr_relay_media_forget(rrconn_t *client, const char *owner, const char *room) {
   struct relay_media **p = &rr_relay_media_maps;
   while (*p) {
      struct relay_media *m = *p;

      if ((!client || m->client == client) && (!owner || !strcmp(m->owner, owner)) && (!room || !strcmp(m->room, room))) {
         if (m->subscribed) {
            dict *d = dict_new();

            if (d) {
               dict_add(d, "msg.type", "media");
               dict_add(d, "media.cmd", "chan-remove");
               dict_add(d, "media.chan-uuid", m->uuid);
               ws_send_dict(NULL, m->client, d, WEBSOCKET_OP_TEXT);
               dict_free(d);
            }
         }
         *p = m->next;
         free(m->frame);
         free(m);
         rr_relay_media_map_count--;
      } else {
         p = &m->next;
      }
   }
}

/* Fragment and encode a bounded media frame into link application messages. */
static bool media_packet(const char *dest, const char *kind, const char *session, const char *username, const char *uuid, const char *room, const uint8_t *frame
   , size_t length, bool setup) {
   if (!length || length > 65535) {
      return false;
   }
   uint32_t sequence = ++rr_relay_media_sequence;

   for (size_t offset = 0 ; offset < length ; offset += RELAY_MEDIA_CHUNK) {
      size_t size = length - offset;

      if (size > RELAY_MEDIA_CHUNK) {
         size = RELAY_MEDIA_CHUNK;
      }
      char *encoded = g_base64_encode(frame + offset, size);
      dict *d = dict_new();

      if (!d || !encoded) {
         dict_free(d);
         g_free(encoded);

         return false;
      }
      dict_add(d, "msg.type", "media");
      dict_add(d, "media.cmd", "frame");
      dict_add(d, "media.chan-uuid", uuid);
      dict_add(d, "media.room", room);
      dict_add_bool(d, "media.setup", setup);
      dict_add(d, "media.data", encoded);
      dict_add_ulong(d, "media.fragment", offset);
      dict_add_ulong(d, "media.total", length);
      dict_add_ulong(d, "media.frame-seq", sequence);
      dict_add_ulong(d, "relay.deadline", now + 2);
      bool ok = rr_relay_packet(dest, kind, session, username, d);
      dict_free(d);
      g_free(encoded);

      if (!ok) {
         return false;
      }
   }

   return true;
}

/* Validate and assemble an ordered, bounded fragment into its mapping. */
bool rr_relay_media_reassemble(struct relay_media *m, dict *d) {
   unsigned long total = dict_get_ulong(d, "media.total", 0), offset = dict_get_ulong(d, "media.fragment", 65536);
   uint32_t sequence = dict_get_ulong(d, "media.frame-seq", 0);
   const char *encoded = dict_get(d, "media.data", "");
   size_t encoded_length = strlen(encoded);

   if (!total || total > 65535 || offset >= total || offset % RELAY_MEDIA_CHUNK || encoded_length > ((RELAY_MEDIA_CHUNK + 2) / 3) * 4 ||
      encoded_length % 4 || strspn(encoded, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") != encoded_length) {
      return false;
   }
   gsize size;
   uint8_t *bytes = g_base64_decode(encoded, &size);
   size_t expected = total - offset;

   if (expected > RELAY_MEDIA_CHUNK) {
      expected = RELAY_MEDIA_CHUNK;
   }

   if (size != expected) {
      g_free(bytes);

      return false;
   }

   if (!offset) {
      free(m->frame);
      m->frame = malloc(total);
      m->size = total;
      m->received = 0;
      m->sequence = sequence;
      m->started = mono_us();
      m->setup = dict_get_bool(d, "media.setup", false);
   }

   if (!m->frame || m->size != total || m->sequence != sequence || offset != m->received ||
      m->setup != dict_get_bool(d, "media.setup", false) || (uint64_t) mono_us() - m->started > (m->setup ? 1000000 : 100000)) {
      g_free(bytes);

      return false;
   }
   memcpy(m->frame + offset, bytes, size);
   m->received += size;
   g_free(bytes);

   return m->received == total;
}

/* Relay outgoing binary media through the owning client mapping. */
void rr_relay_media_outgoing(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_media_binary_t)) {
      return;
   }
   struct remote_session *remote = rr_relay_remote_for(client);

   if (!remote) {
      return;
   }
   rr_media_binary_t *io = (rr_media_binary_t *) data;

   if (io->handled) {
      return;
   }
   struct rr_binframe frame;

   if (rr_binframe_parse(io->frame, io->length, &frame)) {
      return;
   }
   unsigned id = frame.hdr.stream;

   if (!id || id > MAX_MEDIA_CHANNELS) {
      return;
   }
   struct rr_mediachan *channel = &media_channels[id - 1];

   if (!channel->uuid[0] || !media_client_in_channel_room(client, channel) ||
      !chan_id_in_array(channel->direction == RR_BINFRAME_DIR_TX ? client->tx_channels : client->rx_channels, MAX_RX_CHANNELS, id)) {
      return;
   }
   io->handled = true;
   io->result = media_packet(remote->origin, "reply", remote->id, NULL, channel->uuid, channel->room, io->frame, io->length, io->setup);
}

/* Translate and forward incoming client media for a remote owner. */
void rr_relay_media_incoming(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_media_binary_t) || rr_relay_applying || rr_relay_remote_for(client)) {
      return;
   }
   rr_media_binary_t *io = (rr_media_binary_t *) data;
   struct rr_binframe frame;

   if (rr_binframe_parse(io->frame, io->length, &frame)) {
      return;
   }

   for (struct relay_media *m = rr_relay_media_maps ; m ; m = m->next) {
      if (m->client != client || m->stream != frame.hdr.stream) {
         continue;
      }
      io->handled = true;

      if (!m->subscribed || m->direction != RR_BINFRAME_DIR_TX || !ws_client_in_room(client, m->room) || !client->user) {
         return;
      }
      struct relay_client *origin = rr_relay_client_for(client, false);
      uint8_t *copy = malloc(io->length);

      if (!origin || !copy) {
         free(copy);

         return;
      }
      memcpy(copy, io->frame, io->length);
      copy[11] = m->owner_stream;
      io->result = media_packet(m->owner, "request", origin->id, client->user->name, m->uuid, m->room, copy, io->length, io->setup);
      free(copy);

      return;
   }
}

/* Propagate authoritative remote talker changes to interested listeners. */
void rr_relay_remote_talker(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_server_talker_t)) {
      return;
   }
   rr_server_talker_t *query = (rr_server_talker_t *) data;

   if (query->client) {
      return;
   }

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (r->client.authenticated && r->client.is_ptt) {
         query->client = &r->client;

         return;
      }
   }
}

/* Translate remote stream discovery into per-client channel mappings. */
void rr_relay_media_catalog(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_media_catalog_t)) {
      return;
   }
   const rr_media_catalog_t *change = data;

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (!r->client.authenticated) {
         continue;
      }

      if (change->removed) {
         r->proxy_tx[change->channel - media_channels] = false;

         if (media_client_in_channel_room(&r->client, change->channel)) {
            media_send_chan_removed(&r->client, change->channel);
         }
      } else {
         for (unsigned i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
            if (media_channels[i].uuid[0] && media_client_in_channel_room(&r->client, &media_channels[i])) {
               media_send_available(&r->client, &media_channels[i]);
            }
         }
      }
   }
}

/* Report whether relay subscriptions require a local media channel. */
void rr_relay_media_demand(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_media_demand_t)) {
      return;
   }
   rr_media_demand_t *d = (rr_media_demand_t *) data;
   unsigned id = d->channel - media_channels + 1;

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if (chan_id_in_array(d->channel->direction == RR_BINFRAME_DIR_TX ? r->client.tx_channels : r->client.rx_channels, d->channel->direction ==
         RR_BINFRAME_DIR_TX ? MAX_TX_CHANNELS : MAX_RX_CHANNELS, id)
         && media_client_in_channel_room(&r->client, d->channel)) {
         d->interested = true;

         return;
      }
   }
}

/* Fan out a local media frame only to interested remote branches. */
void rr_relay_media_fanout(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_media_fanout_t)) {
      return;
   }
   const rr_media_fanout_t *f = data;
   unsigned id = f->channel - media_channels + 1;

   for (struct remote_session *r = rr_relay_remotes ; r ; r = r->next) {
      if ((f->target && f->target != &r->client) || f->exclude == &r->client
         || !chan_id_in_array(f->channel->direction == RR_BINFRAME_DIR_TX ? r->client.tx_channels : r->client.rx_channels, f->channel->direction ==
         RR_BINFRAME_DIR_TX ? MAX_TX_CHANNELS : MAX_RX_CHANNELS, id)
         || !media_client_in_channel_room(&r->client, f->channel)) {
         continue;
      }
      uint8_t *copy = malloc(f->length);

      if (!copy) {
         continue;
      }
      memcpy(copy, f->frame, f->length);
      struct rr_binframe parsed;

      if (rr_binframe_parse(copy, f->length, &parsed)) {
         free(copy);
         continue;
      }
      rr_binframe_pack_hdr(copy, f->length, parsed.hdr.subsystem, parsed.hdr.codec, parsed.hdr.direction, parsed.hdr.vfo, parsed.hdr.rig, parsed.hdr.stream,
         rr_media_next_sequence(&r->client, &parsed.hdr), parsed.len, parsed.hdr.ts);
      struct mg_str bytes = {
         .buf = (char *)copy, .len = f->length
      };

      if (f->setup) {
         ws_send_setup_to_cptr(NULL, &r->client, &bytes);
      } else {
         ws_send_to_cptr(NULL, &r->client, &bytes, WEBSOCKET_OP_BINARY);
      }
      free(copy);
   }
}

/* Check whether a remote session still has a matching proxy TX subscription. */
bool rr_relay_proxy_tx_subscribed(struct remote_session *r, const char *room) {
   rrconn_t *client = &r->client;

   if (!room || !client->authenticated || !client->user || !client->user->enabled || client->user->is_muted ||
      client->user->password_change_required || (client->user->password_expires && client->user->password_expires <= now)) {
      return false;
   }

   for (unsigned i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *channel = &media_channels[i];

      if (r->proxy_tx[i] && channel->uuid[0] && channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO &&
         channel->direction == RR_BINFRAME_DIR_TX && !strcmp(channel->room, room) &&
         media_client_in_channel_room(client, channel) && chan_id_in_array(client->tx_channels, MAX_TX_CHANNELS, i + 1)) {
         return true;
      }
   }

   return false;
}

/* Check accepted-audio liveness and remaining proxy TX authority. */
bool rrserver_relay_proxy_tx_active(rrconn_t *client, const char *room, uint64_t at) {
   struct remote_session *r = rr_relay_remote_for(client);

   if (!r || !client->is_ptt || !rr_relay_proxy_tx_subscribed(r, room) || strcmp(client->ptt_room, room) ||
      !has_priv(client->user->uid, "admin|owner|tx|noob") || !rrserver_supervision_allowed(client)) {
      return false;
   }
   uint64_t heard = r->proxy_frame > r->proxy_started ? r->proxy_frame : r->proxy_started;

   return heard && at >= heard && at - heard < UINT64_C(3000000);
}

/* Only frames accepted by the authoritative codec, room, permission and PTT checks keep proxy TX live. Neither control traffic nor incomplete fragments count.
 * PARITY: rustyrig-www/js/webui.audio.js:webui_append_tx_samples uses this WS path. */
void rr_relay_proxy_tx_frame(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   struct remote_session *r = rr_relay_remote_for(client);
   struct rr_binframe frame;

   if (!r || !client->is_ptt || rr_binframe_parse(data, length, &frame) ||
      frame.hdr.subsystem != RR_BINFRAME_SUBSYS_AUDIO || frame.hdr.direction != RR_BINFRAME_DIR_TX ||
      !frame.hdr.stream || frame.hdr.stream > MAX_MEDIA_CHANNELS || !frame.len || !r->proxy_tx[frame.hdr.stream - 1]) {
      return;
   }
   r->proxy_frame = mono_us();
}
#endif /* USE_MONGOOSE */

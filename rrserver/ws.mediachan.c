//
// librrprotocol/ws.mediachan.c: media channel registry, subscribe protocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Server side of the media channel protocol:
//
//  - The program (rrserver) creates channels for each streamable media
//    direction (e.g. audio RX on VFO A, audio TX for the rig) with
//    media_chan_add(); each gets a UUID.
//  - Clients learn about channels via `media.available` messages (sent
//    after auth and on demand with `media.cmd: list`).
//  - Clients subscribe with `media.cmd: subscribe` + `media.chan-uuid`;
//    the server records the channel in the connection's rx_channels /
//    tx_channels table and confirms with `media.cmd: subscribed`.
//
// PARITY: rustyrig-www/js/webui.media.js
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librustyaxe/transport.h>
#include <librrprotocol/traffic.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/codecneg.h>
#include <librrprotocol/media.health.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/ws.mediachan.h>

extern time_t now;

const char *media_chan_description(const struct rr_mediachan *channel, const char *station, char *out, size_t capacity) {
   if (!channel) {
      snprintf(out, capacity, "unknown channel");

      return out;
   }
   char fallback[64], vfo[16] = "";

   if (channel->vfo < 26) {
      snprintf(vfo, sizeof(vfo), ".vfo-%c", 'a' + channel->vfo);
   }
   snprintf(fallback, sizeof(fallback), "rig%u%s.%s", channel->rig, vfo, channel->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx");
   snprintf(out, capacity, "%s%s%s (room=%s codec=%s uuid=%s)", station && *station ? station : "", station
      && *station ? "." : "", channel->name[0] ? channel->name : fallback, channel->room[0] ? channel->room : "unassigned", channel->codec[0] ? channel->codec :
      "unknown", channel->uuid);

   return out;
}

static bool media_codec_list_has(const char *list, const char *codec) {
   if (!list || !codec || strlen(codec) != 4) {
      return false;
   }

   const char *p = list;
   while (*p) {
      while (*p == ' ') {
         p++;
      }

      if (!*p) {
         break;
      }

      const char *start = p;
      while (*p && *p != ' ') {
         p++;
      }

      if ((size_t)(p - start) == 4 && memcmp(start, codec, 4) == 0) {
         return true;
      }
   }
   return false;
}

static bool media_client_supports_codec(rrconn_t *cptr, const char *codec) {
   // Keep compatibility with clients predating media.capab. Once a client
   // advertises capabilities, enforce them for every channel selection.
   return !cptr || !cptr->media_codecs[0] || media_codec_list_has(cptr->media_codecs, codec);
}

static bool media_codec_valid(const char *codec) {
   // NONE is client-local unsubscribe intent; ---- is only a display label.
   return codec && strlen(codec) == 4 && strcasecmp(codec, "none") && strcmp(codec, "----");
}

static bool media_server_supports_codec(const char *codec) {
   if (!media_codec_valid(codec)) {
      return false;
   }

   const char *configured = cfg_get_exp("codecs.allowed");
   char *allowed = codec_filter_test_mode(configured, cfg_get_bool("audio.test-mode", true));
   free((void *)configured);
   bool supported = allowed && media_codec_list_has(allowed, codec);
   free(allowed);

   return supported;
}

static bool media_channel_all_clients_support(struct rr_mediachan *cp, const char *codec) {
   if (!cp || !codec) {
      return false;
   }

   u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;
   rrconn_t *cur = http_client_list;
   while (cur) {
      // TX audio is a shared VFO stream. Only clients subscribed to this
      // concrete media channel and its room constrain its codec.
      bool relevant = cp->direction == RR_BINFRAME_DIR_TX ?
         chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id);

      if (relevant && media_client_in_channel_room(cur, cp) && !media_client_supports_codec(cur, codec)) {
         return false;
      }
      cur = cur->next;
   }
   return true;
}

// RX channels can be viewed through a mapped subroom; TX always belongs
// exclusively to the server-owned base room.  Project the latest matching
// RX membership in media.available so client selection remains room-scoped.
static const char *media_client_channel_room(const rrconn_t *client, const struct rr_mediachan *channel) {
   static char selected[128];

   if (!client || !channel) {
      return NULL;
   }

   if (!channel->room[0]) {
      return channel->room;
   }

   if (channel->direction != RR_BINFRAME_DIR_RX || (channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO &&
      !(channel->subsystem == RR_BINFRAME_SUBSYS_MODEM && (!strcmp(channel->codec, "nmea") || !strcmp(channel->codec, "gpsp"))))) {
      return ws_client_in_room(client, channel->room) ? channel->room : NULL;
   }
   char joined[AUTOJOIN_LEN];
   snprintf(joined, sizeof(joined), "%s", client->rooms);
   selected[0] = '\0';
   char *save = NULL;

   for (char *room = strtok_r(joined, ",", &save) ; room ; room = strtok_r(NULL, ",", &save)) {
      if (!strcasecmp(room, channel->room) ||
         (ws_room_same_rig(room, channel->room) && channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO &&
         channel->vfo == RR_BINFRAME_VFO_NA && ws_room_vfo_mask(room)) ||
         (ws_room_same_rig(room, channel->room) && channel->vfo < 32 &&
         (ws_room_vfo_mask(room) & (UINT32_C(1) << channel->vfo))) ||
         (ws_room_same_rig(room, channel->room) && channel->subsystem == RR_BINFRAME_SUBSYS_MODEM &&
         (!strcmp(channel->codec, "nmea") || !strcmp(channel->codec, "gpsp")))) {
         snprintf(selected, sizeof(selected), "%s", room);
      }
   }

   return selected[0] ? selected : NULL;
}

bool media_client_in_channel_room(const rrconn_t *cptr, const struct rr_mediachan *cp) {
   if (!media_client_channel_room(cptr, cp)) {
      return false;
   }
   rr_media_access_t access = {
      .channel = cp, .allowed = true
   };
   event_emit_binary(RR_MEDIA_ACCESS_EVENT, (rrconn_t *) cptr, &access, sizeof(access));

   return access.allowed;
}

// Pick the channel format when its first subscriber arrives. The channel's
// codec is shared, so the initial value must come from the intersection of
// the server list and this client's negotiated capabilities. Subsequent
// codec changes still require an explicit media.codec request.
static bool media_init_channel_codec(rrconn_t *cptr, struct rr_mediachan *cp) {
   if (!cptr || !cp || cp->codec[0] != '\0') {
      return cp && cp->codec[0] != '\0';
   }

   const char *configured_codecs = cfg_get_exp("codecs.allowed");
   char *server_codecs = codec_filter_test_mode(configured_codecs, cfg_get_bool("audio.test-mode", true));
   free((void *)configured_codecs);

   if (!server_codecs || !*server_codecs) {
      free((void *)server_codecs);

      return false;
   }
   char *common = cptr->media_codecs[0] ? codec_filter_common(server_codecs, cptr->media_codecs) : strdup(server_codecs);
   free((void *)server_codecs);

   if (!common || strlen(common) < 4) {
      free(common);

      return false;
   }

   char codec[5] = {
      0
   };
   const char *selected = common;

   if (cp->direction == RR_BINFRAME_DIR_TX && codec_is_test_variant(selected)) {
      /* Test codecs generate radio RX audio. A radio TX channel must start with a codec fed by the client microphone instead. */
      while (*selected) {
         if (codec_is_test_variant(selected)) {
            while (*selected && *selected != ' ') {
               selected++;
            }
            while (*selected == ' ') {
               selected++;
            }
            continue;
         }
         break;
      }

      if (!*selected) {
         free(common);

         return false;
      }
   }
   memcpy(codec, selected, 4);
   free(common);

   dict *sel = dict_new();

   if (sel) {
      dict_add(sel, "media.codec", codec);
      dict_add_ulong(sel, "media.dir", cp->direction);
      dict_add(sel, "media.chan-uuid", cp->uuid);
      event_emit_dict("media.codec-select", cptr, sel);
      dict_free(sel);
   }
   snprintf(cp->codec, sizeof(cp->codec), "%s", codec);
   char description[512];
   Log(LOG_INFO, "ws.media", "Selected initial codec %s for channel %s from %s's negotiated capabilities", cp->codec, media_chan_description(cp, NULL,
      description, sizeof(description)), cptr->chatname[0] ? cptr->chatname : "client");

   return true;
}

#ifndef MAX_MEDIA_CHANNELS
#define MAX_MEDIA_CHANNELS 64
#endif

// The registry; the program (rrserver) fills this in via media_chan_add()
struct rr_mediachan media_channels[MAX_MEDIA_CHANNELS];

// Fill in a uuid for a new channel: use a random-ish but stable string
static void media_gen_uuid(char *out, size_t len) {
   static uint64_t counter = 0;
   uint64_t serial = counter++;
   snprintf(out, len, "%llx-%04llx", (unsigned long long)now, (unsigned long long)((uintptr_t) &media_channels[serial % MAX_MEDIA_CHANNELS] + serial));
}

struct rr_mediachan *media_chan_add(uint8_t subsystem, uint8_t direction, uint8_t vfo, uint8_t rig, const char *codec, const char *descr) {
   // Audio codecs remain interchangeable on one routing quadruple. Fixed
   // MODEM formats are independent services (gpsp summaries and raw nmea).
   struct rr_mediachan *cp = media_chan_find(subsystem, direction, vfo, rig);

   if (subsystem == RR_BINFRAME_SUBSYS_MODEM && codec) {
      cp = NULL;

      for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
         struct rr_mediachan *candidate = &media_channels[i];

         if (candidate->uuid[0] && candidate->subsystem == subsystem &&
            candidate->direction == direction && candidate->vfo == vfo && candidate->rig == rig && !strcmp(candidate->codec, codec)) {
            cp = candidate;
            break;
         }
      }
   }

   if (cp) {
      return cp;
   }

   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0') {
         continue;
      }
      cp = &media_channels[i];
      memset(cp, 0, sizeof(*cp));
      media_gen_uuid(cp->uuid, sizeof(cp->uuid));
      cp->subsystem = subsystem;
      cp->direction = direction;
      cp->vfo = vfo;
      cp->rig = rig;

      const char *kind = direction == RR_BINFRAME_DIR_TX ? "tx" : "rx";

      if (vfo != RR_BINFRAME_VFO_NA) {
         snprintf(cp->name, sizeof(cp->name), "rig%u.vfo_%c.%s", rig, (char)('a' + vfo), kind);
      } else {
         snprintf(cp->name, sizeof(cp->name), "rig%u.%s", rig, kind);
      }

      if (codec) {
         snprintf(cp->codec, sizeof(cp->codec), "%s", codec);
      }

      if (descr) {
         snprintf(cp->descr, sizeof(cp->descr), "%s", descr);
      }
      char description[512];
      Log(LOG_DEBUG, "ws.media", "Added media channel %s: subsys 0x%02X dir %s vfo %u rig %u (%s)", media_chan_description(cp, NULL, description, sizeof(
         description)), subsystem, (direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), vfo, rig, (descr ? descr : "-"));

      return cp;
   }

   Log(LOG_CRIT, "ws.media", "media_chan_add: channel table full!");

   return NULL;
}

struct rr_mediachan *media_chan_find(uint8_t subsystem, uint8_t direction, uint8_t vfo, uint8_t rig) {
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *cp = &media_channels[i];

      if (cp->uuid[0] == '\0') {
         continue;
      }

      if (cp->subsystem == subsystem && cp->direction == direction && cp->vfo == vfo && cp->rig == rig) {
         return cp;
      }
   }

   return NULL;
}

struct rr_mediachan *media_chan_find_uuid(const char *uuid) {
   if (!uuid || uuid[0] == '\0') {
      return NULL;
   }

   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0' && strcasecmp(media_channels[i].uuid, uuid) == 0) {
         return &media_channels[i];
      }
   }

   return NULL;
}

void media_channels_free(void) {
   memset(media_channels, 0, sizeof(media_channels));
}

// Remove a channel by uuid; returns false on OK. Does not notify clients -
// pair with media_send_chan_removed_all() when the removal is user-visible.
bool media_chan_remove(const char *uuid) {
   struct rr_mediachan *cp = media_chan_find_uuid(uuid);

   if (!cp) {
      return true;
   }
   char description[512];
   Log(LOG_INFO, "ws.media", "Removed media channel %s (subsys 0x%02X dir %s vfo %u rig %u)", media_chan_description(cp, NULL, description, sizeof(description))
      , cp->subsystem, (cp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), cp->vfo, cp->rig);
   memset(cp, 0, sizeof(*cp));

   return false;
}

// Send one media.chan-remove message for channel `cp` to client `cptr`
bool media_send_chan_removed(rrconn_t *cptr, struct rr_mediachan *cp) {
   if (!cptr || !cp || cp->uuid[0] == '\0') {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "chan-remove");
   dict_add(d, "media.chan-uuid", cp->uuid);
   dict_add_ulong(d, "media.ts", now);
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

// Notify every connected client that channel `cp` was removed
void media_send_chan_removed_all(struct rr_mediachan *cp) {
   if (!cp || cp->uuid[0] == '\0') {
      return;
   }
   rr_media_catalog_t change = {
      .channel = cp, .removed = true
   };
   event_emit_binary(RR_MEDIA_CATALOG_EVENT, NULL, &change, sizeof(change));
   rrconn_t *cur = http_client_list;

   while (cur) {
      if (cur->is_ws && cur->authenticated) {
         media_send_chan_removed(cur, cp);
      }
      cur = cur->next;
   }
}

// Send one media.available message to a client
bool media_send_available(rrconn_t *cptr, struct rr_mediachan *cp) {
   if (!cptr || !cp || cp->uuid[0] == '\0') {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "available");
   dict_add(d, "media.chan-uuid", cp->uuid);
   dict_add_ulong(d, "media.subsys", cp->subsystem);
   dict_add_ulong(d, "media.dir", cp->direction);
   dict_add_ulong(d, "media.vfo", cp->vfo);
   dict_add_ulong(d, "media.rig", cp->rig);

   if (cp->room[0]) {
      const char *view_room = media_client_channel_room(cptr, cp);
      dict_add(d, "media.room", view_room ? view_room : cp->room);
      dict_add(d, "media.control-room", cp->room);
      dict_add_bool(d, "media.joined", media_client_in_channel_room(cptr, cp));
   }

   if (cp->rig_uuid[0]) {
      dict_add(d, "media.rig-uuid", cp->rig_uuid);
   }

   if (cp->vfo_uuid[0]) {
      dict_add(d, "media.vfo-uuid", cp->vfo_uuid);
   }
   dict_add_ulong(d, "media.ts", now);

   if (cp->name[0] != '\0') {
      dict_add(d, "media.name", cp->name);
   }

   if (cp->codec[0] != '\0') {
      dict_add(d, "media.codec", cp->codec);
   }

   if (cp->profiles && cp->profiles[0]) {
      dict_add(d, "media.profiles", cp->profiles);
   }

   if (cp->descr[0] != '\0') {
      dict_add(d, "media.descr", cp->descr);
   }
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool media_send_available_all(rrconn_t *cptr) {
   if (!cptr) {
      rr_media_catalog_t change = {
         0
      };
      event_emit_binary(RR_MEDIA_CATALOG_EVENT, NULL, &change, sizeof(change));
      rrconn_t *cur = http_client_list;
      while (cur) {
         // PARITY: srv.send.c ws_broadcast_dict recipient filtering.
         // HTTP page/asset connections must never receive WebSocket frames.
         if (cur->is_ws && cur->authenticated) {
            media_send_available_all(cur);
         }
         cur = cur->next;
      }
      return false;
   }

   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0') {
         media_send_available(cptr, &media_channels[i]);
      }
   }

   return false;
}

// Helper: is chan_id already in the user's channel array?
static bool chan_in_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   return chan_id_in_array(arr, max, chan_id);
}

// Is chan_id present in a rx_channels[]/tx_channels[] style array?
// Exported so the binary frame router can validate source subscriptions.
bool chan_id_in_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   if (!arr || chan_id == 0) {
      return false;
   }

   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == chan_id) {
         return true;
      }
   }

   return false;
}

// Helper: store chan_id in the first free slot of the user's channel array
static bool chan_add_to_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   if (chan_in_array(arr, max, chan_id)) {
      return false;             // already subscribed
   }

   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == 0) {
         arr[i] = chan_id;

         return false;
      }
   }

   return true;
}

// Helper: remove chan_id from the user's channel array
static void chan_del_from_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == chan_id) {
         arr[i] = 0;

         return;
      }
   }
}

// PART revokes every subscription in that room, including manual selections.
void media_part_room(rrconn_t *cptr, const char *room) {
   if (!cptr || !room) {
      return;
   }

   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *cp = &media_channels[i];

      if (!cp->uuid[0] || !cp->room[0]) {
         continue;
      }

      bool same = !strcasecmp(cp->room, room) || (cp->direction == RR_BINFRAME_DIR_RX && ws_room_same_rig(room, cp->room));

      if (!same) {
         continue;
      }
      u_int32_t id = i + 1;
      bool tx = cp->direction == RR_BINFRAME_DIR_TX;
      u_int32_t *channels = tx ? cptr->tx_channels : cptr->rx_channels;
      int max = tx ? MAX_TX_CHANNELS : MAX_RX_CHANNELS;

      if (!chan_in_array(channels, max, id)) {
         continue;
      }
      chan_del_from_array(channels, max, id);
      dict *reply = dict_new();
      dict_add(reply, "msg.type", "media");
      dict_add(reply, "media.cmd", "unsubscribed");
      dict_add(reply, "media.chan-uuid", cp->uuid);
      ws_send_dict(NULL, cptr, reply, WEBSOCKET_OP_TEXT);
      event_emit_dict("media.unsubscribed", cptr, reply);
      dict_free(reply);
   }
}


// Fan out one media payload to every connection subscribed to channel `cp`.
// The server owns the wire header values (see doc/media-frames.md).
bool ws_media_channel_has_subscribers(const struct rr_mediachan *cp) {
   if (!cp || cp->uuid[0] == '\0') {
      return false;
   }
   u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;
   rrconn_t *cur = http_client_list;
   while (cur) {
      bool subscribed = cp->direction == RR_BINFRAME_DIR_TX ?
         chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id);

      if (cur->is_ws && cur->authenticated && subscribed && media_client_in_channel_room(cur, cp)) {
         return true;
      }
      cur = cur->next;
   }
   rr_media_demand_t query = {
      .channel = cp
   };
   event_emit_binary(RR_MEDIA_DEMAND_EVENT, NULL, &query, sizeof(query));

   return query.interested;
}

bool ws_media_broadcast_subscribed(struct rr_mediachan *cp, const uint8_t *payload, size_t len, const char codec[4]) {
   return ws_media_broadcast_subscribed_except(cp, NULL, payload, len, codec);
}

static bool ws_media_send_frame_filtered(struct rr_mediachan *cp, rrconn_t *target, rrconn_t *exclude, const uint8_t *payload, size_t len, const char codec[4],
   bool setup) {
   if (!cp || cp->uuid[0] == '\0' || !payload || len > RR_BINFRAME_MAX_PAYLOAD) {
      return true;
   }

   u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;
   char codecbuf[4] = {
      0
   };

   if (codec && codec[0] != '\0') {
      memcpy(codecbuf, codec, 4);
   } else if (cp->codec[0] != '\0') {
      memcpy(codecbuf, cp->codec, 4);
   }
   uint8_t *frame = NULL;
   int flen = rr_binframe_frame(&frame, cp->subsystem, codecbuf, cp->direction, cp->vfo, cp->rig, (uint8_t) (chan_id & 0xFF), 0, mono_us(), payload, len);

   if (flen < 0) {
      return true;
   }

   rr_media_fanout_t fanout = {
      .channel = cp, .frame = frame, .length = flen,
      .target = target, .exclude = exclude, .setup = setup
   };
   event_emit_binary(RR_MEDIA_FANOUT_EVENT, NULL, &fanout, sizeof(fanout));
   rrconn_t *cur = target ? target : http_client_list;
   while (cur) {
      if ((!target || cur == target) && cur != exclude && cur->is_ws &&
         cur->authenticated && cur->native_transport &&
         rr_transport_state(cur->native_transport) == RR_TRANSPORT_OPEN &&
         ((cp->direction == RR_BINFRAME_DIR_TX &&
         chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id)) ||
         (cp->direction == RR_BINFRAME_DIR_RX && chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id))) && media_client_in_channel_room(cur, cp)) {
         struct rr_binframe parsed;
         rr_binframe_parse(frame, flen, &parsed);
         uint32_t sequence = rr_media_next_sequence(cur, &parsed.hdr);
         rr_binframe_pack_hdr(frame, flen, cp->subsystem, codecbuf, cp->direction, cp->vfo, cp->rig, chan_id, sequence, len, parsed.hdr.ts);
         /* Setup frames are reliable control data; live media is realtime. Avoid letting an overloaded subscriber accumulate stale media. */
         size_t queued = rr_transport_queued_bytes(cur->native_transport);
         size_t limit = setup ? 262144u : (cp->subsystem == RR_BINFRAME_SUBSYS_AUDIO ? 8192u : 262144u);

         if ((size_t)flen <= limit && queued <= limit - (size_t)flen) {
            if (rr_transport_send_binary_message(cur->native_transport, frame, (size_t)flen) == RR_TRANSPORT_OK) {
               rr_traffic_count(cur, true, true, (size_t)flen);
            }
         } else if (setup) {
            event_emit("protocol.backpressure", cur, "Media setup queue full");
         }
      }
      cur = target ? NULL : cur->next;
   }
   free(frame);

   return false;
}

bool ws_media_broadcast_subscribed_except(struct rr_mediachan *cp, rrconn_t *exclude, const uint8_t *payload, size_t len, const char codec[4]) {
   return ws_media_send_frame_filtered(cp, NULL, exclude, payload, len, codec, false);
}

bool ws_media_send_frame(struct rr_mediachan *cp, rrconn_t *cptr, const uint8_t *payload, size_t len, const char codec[4]) {
   return ws_media_send_frame_filtered(cp, cptr, NULL, payload, len, codec, false);
}

bool ws_media_send_setup_frame(struct rr_mediachan *cp, rrconn_t *cptr, const uint8_t *payload, size_t len, const char codec[4]) {
   return ws_media_send_frame_filtered(cp, cptr, NULL, payload, len, codec, true);
}

bool media_source_authorized(rrconn_t *cptr) {
   if (!cptr || !cptr->authenticated || !cptr->user) {
      return false;
   }

   if (has_priv(cptr->user->uid, "media.source")) {
      return true;
   }

   return client_has_flag(cptr, FLAG_VIDEO_SOURCE) && has_priv(cptr->user->uid, "video-src");
}

bool ws_handle_mediachan_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return false;
   }
   const char *media_cmd = dict_get(d, "media.cmd", NULL);
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);

   if (!media_cmd) {
      Log(LOG_DEBUG, "ws.media", "media message without media.cmd");

      return false;
   }

   if (!strcasecmp(media_cmd, "profile")) {
      struct rr_mediachan *cp = uuid ? media_chan_find_uuid(uuid) : NULL;

      if (!cptr->authenticated || !cp || cp->subsystem != RR_BINFRAME_SUBSYS_VIDEO || !media_client_in_channel_room(cptr, cp)) {
         return false;
      }

      if (!chan_id_in_array(cptr->rx_channels, MAX_RX_CHANNELS, cp - media_channels + 1)) {
         return false;
      }
      rr_media_profile_request_t request = {
         .message = d
      };
      event_emit_binary("media.profile.request", cptr, &request, sizeof(request));

      if (!request.handled) {
         ws_send_error(cptr, "Video profile selection is unavailable");
      }

      return request.handled && request.result;
   }

   if (!strcasecmp(media_cmd, "feedback")) {
      long stream = dict_get_long(d, "media.stream", 0);

      if (!cptr->authenticated || stream < 1 || stream > MAX_MEDIA_CHANNELS) {
         return false;
      }
      struct rr_mediachan *channel = &media_channels[stream - 1];

      if (!channel->uuid[0] || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
         channel->direction != RR_BINFRAME_DIR_RX || !chan_id_in_array(cptr->rx_channels, MAX_RX_CHANNELS, stream) ||
         !media_client_in_channel_room(cptr, channel) || !rr_media_feedback(cptr, d, stream, channel->direction, channel->codec, mono_us())) {
         return false;
      }

      return true;
   }

   if (strcasecmp(media_cmd, "capab") == 0) {
      const char *codecs = dict_get(d, "media.codecs", NULL);

      if (!codecs || !*codecs || strlen(codecs) >= sizeof(cptr->media_codecs)) {
         ws_send_error(cptr, "Invalid media codec capability list");

         return false;
      }
      snprintf(cptr->media_codecs, sizeof(cptr->media_codecs), "%s", codecs);

      const char *configured_codecs = cfg_get_exp("codecs.allowed");
      char *server_codecs = codec_filter_test_mode(configured_codecs, cfg_get_bool("audio.test-mode", true));
      free((void *)configured_codecs);
      char *common = server_codecs ? codec_filter_common(cptr->media_codecs, server_codecs) : NULL;
      free((void *)server_codecs);

      if (!common || !*common) {
         free(common);
         ws_send_error(cptr, "No audio codecs in common with server");

         return false;
      }

      dict *ack = dict_new();
      dict_add(ack, "msg.type", "media");
      dict_add(ack, "media.cmd", "isupport");
      dict_add(ack, "media.codecs", common);
      char preferred[5] = {
         0
      };
      memcpy(preferred, common, 4);
      dict_add(ack, "media.preferred", preferred);
      ws_send_dict(NULL, cptr, ack, WEBSOCKET_OP_TEXT);
      dict_free(ack);
      free(common);
      // Refresh channel metadata and this session's room membership.
      media_send_available_all(NULL);

      return true;
   }

   if (strcasecmp(media_cmd, "source") == 0) {
      // Media source registration (rrmedia, fwdsp feeds, remote relays).
      // Generic sources need media.source. A connection claiming the
      // video-source role also needs the account's video-src privilege.
      if (!media_source_authorized(cptr)) {
         Log(LOG_AUDIT, "auth", "Denied media.source from %s on cptr:<%p> (missing media.source/video-src privilege or unauthenticated)", (cptr->chatname[0] !=
            '\0' ? cptr->chatname : "(unknown)"), cptr);
         ws_send_error(cptr, "media.source denied: account requires media.source, and video-src for a video source");

         return false;
      }
      // No uuid = register as a source for all channels (like media.available
      // suggests); with a uuid, register for that one channel.
      client_set_flag(cptr, FLAG_MEDIA_SOURCE); // redundant; belt & braces
      cptr->connection_type = CONN_AUDIO_RX; // legacy marker: feeds media

      if (uuid && uuid[0] != '\0') {
         struct rr_mediachan *cp = media_chan_find_uuid(uuid);

         if (!cp) {
            ws_send_error(cptr, "media.%s: no channel with UUID %s; use /media list", media_cmd, uuid ? uuid : "(missing)");

            return false;
         }
         // A source subscribes to its feed channel in the push direction
         u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;
         bool oom = (cp->direction == RR_BINFRAME_DIR_TX ?
            chan_add_to_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_add_to_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id));

         if (oom) {
            ws_send_error(cptr, "media.%s: subscription limit reached for channel %s; unsubscribe another channel "
               "first", media_cmd, uuid ? uuid : "(requested routing)");

            return false;
         }
      }
      dict *ack = dict_new();
      dict_add(ack, "msg.type", "media");
      dict_add(ack, "media.cmd", "source-ok");
      dict_add_ulong(ack, "media.ts", now);

      if (uuid && uuid[0] != '\0') {
         dict_add(ack, "media.chan-uuid", uuid);
      }
      ws_send_dict(NULL, cptr, ack, WEBSOCKET_OP_TEXT);
      dict_free(ack);
      Log(LOG_INFO, "ws.media", "Media source registered: %s (cptr:<%p>, channel %s)", cptr->chatname, cptr, (uuid && uuid[0] ? uuid : "<all>"));

      // Let the program (rrserver) know a source joined, e.g. to hook the
      // fwdsp pipeline up to this connection.
      event_emit_dict("media.source", cptr, d);

      return true;
   } else if (strcasecmp(media_cmd, "list") == 0) {
      // Client wants the (possibly updated) channel list
      media_send_available_all(cptr);

      return true;
   } else if (strcasecmp(media_cmd, "subscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      // PARITY: rustyrig-www/js/webui.media.js (subscribeMediaChannel)
      // A subscribe without a (known) uuid is a channel creation request:
      // the client tells us what to make via media.subsys/dir/vfo/rig (the
      // routing quadruple), we generate the uuid per the existing logic in
      // media_chan_add(). Defaults: RX audio on the first rig.
      if (!cp) {
         if (!cptr->user || !has_priv(cptr->user->uid, "admin|owner|media.source")) {
            ws_send_error(cptr, "Media channel creation requires admin, owner or media.source");

            return false;
         }
         uint32_t subsys = dict_get_ulong(d, "media.subsys", RR_BINFRAME_SUBSYS_AUDIO);
         uint32_t dir = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_RX);
         uint32_t vfo = dict_get_ulong(d, "media.vfo", 0);
         uint32_t rig = dict_get_ulong(d, "media.rig", 0);
         const char *descr = dict_get(d, "media.descr", NULL);
         const char *codec = dict_get(d, "media.codec", NULL);

         if (subsys > UINT8_MAX || dir > RR_BINFRAME_DIR_TX || vfo > UINT8_MAX || rig > UINT8_MAX) {
            ws_send_error(cptr, "media.subscribe: invalid routing (subsystem %u, direction %u, VFO %u, rig %u)", subsys, dir, vfo, rig);

            return false;
         }

         // Validate before inserting anything into the shared registry. An
         // omitted codec is negotiated below, just like a UUID subscription.
         if (codec && (!media_codec_valid(codec) ||
            (subsys == RR_BINFRAME_SUBSYS_AUDIO &&
            (!media_server_supports_codec(codec) ||
            !media_client_supports_codec(cptr, codec) || (dir == RR_BINFRAME_DIR_TX && codec_is_test_variant(codec)))))) {
            ws_send_error(cptr, "media.subscribe: unsupported codec %s", codec ? codec : "(missing)");

            return false;
         }

         cp = media_chan_add((uint8_t) subsys, (uint8_t) dir, (uint8_t) vfo, (uint8_t) rig, codec, descr);

         if (!cp) {
            Log(LOG_WARN, "ws.media", "Subscribe-create failed for %s (table full?)", cptr->chatname);
            ws_send_error(cptr, "media.%s: no channel with UUID %s; use /media list", media_cmd, uuid ? uuid : "(missing)");

            return false;
         }
         // Tell everyone (including the requester) about the new channel
         media_send_available_all(cptr);
      }

      if (!media_client_in_channel_room(cptr, cp)) {
         ws_send_error(cptr, "Join room %s before subscribing to its media", cp->room);

         return false;
      }

      // Channel id is 1 + table index; 0 means "no channel" in the
      // rx_channels/tx_channels arrays
      if (!cp->codec[0] && !media_init_channel_codec(cptr, cp)) {
         ws_send_error(cptr, "No negotiated codec is available for media channel %s (%s) in room %s", cp->name, cp->uuid, cp->room);

         return false;
      }
      media_send_available_all(NULL);
      u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;
      bool is_tx = (cp->direction == RR_BINFRAME_DIR_TX);

      if (cp->subsystem == RR_BINFRAME_SUBSYS_AUDIO && cp->codec[0] && !media_client_supports_codec(cptr, cp->codec)) {
         ws_send_error(cptr, "This client does not support codec %s required by media channel %s (%s)", cp->codec, cp->name, cp->uuid);

         return false;
      }
      bool already_subscribed = is_tx ? chan_in_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_in_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id);
      bool oom = (is_tx ? chan_add_to_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_add_to_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id));

      if (oom) {
         char description[512];
         Log(LOG_WARN, "ws.media", "No free channel slots for %s subscribing to %s", cptr->chatname, media_chan_description(cp, NULL, description, sizeof(
            description)));
         ws_send_error(cptr, "media.%s: subscription limit reached for channel %s; unsubscribe another channel " "first", media_cmd, uuid ? uuid :
            "(requested routing)");

         return false;
      }

      if (cp->subsystem == RR_BINFRAME_SUBSYS_AUDIO) {
         snprintf(is_tx ? cptr->codec_tx : cptr->codec_rx, sizeof(cptr->codec_tx), "%s", cp->codec);
      }
      dict *sub = dict_new();
      dict_add(sub, "msg.type", "media");
      dict_add(sub, "media.cmd", "subscribed");
      dict_add(sub, "media.chan-uuid", cp->uuid);
      dict_add_ulong(sub, "media.stream", chan_id);
      dict_add_ulong(sub, "media.subsys", cp->subsystem);
      dict_add_ulong(sub, "media.dir", cp->direction);
      dict_add_ulong(sub, "media.vfo", cp->vfo);
      dict_add_ulong(sub, "media.rig", cp->rig);
      dict_add_ulong(sub, "media.ts", now);

      if (cp->codec[0] != '\0') {
         dict_add(sub, "media.codec", cp->codec);
      }
      ws_send_dict(NULL, cptr, sub, WEBSOCKET_OP_TEXT);

      if (!already_subscribed) {
         event_emit_dict("media.subscribed", cptr, sub);
      }
      dict_free(sub);
      char description[512];
      Log(LOG_DEBUG, "ws.media", "Subscribed %s to channel %s (stream %u)", cptr->chatname, media_chan_description(cp, NULL, description, sizeof(description)),
         chan_id);
      // A new subscriber may change the set of codecs that can safely be
      // used for a shared TX stream. Re-announce the channel state so every
      // client refreshes its capability view.
      media_send_available_all(NULL);

      return true;
   } else if (strcasecmp(media_cmd, "unsubscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      if (!cp) {
         ws_send_error(cptr, "media.%s: no channel with UUID %s; use /media list", media_cmd, uuid ? uuid : "(missing)");

         return false;
      }

      u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;

      if (cp->direction == RR_BINFRAME_DIR_TX) {
         chan_del_from_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id);
      } else {
         chan_del_from_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id);
      }
      dict *unsub = dict_new();
      dict_add(unsub, "msg.type", "media");
      dict_add(unsub, "media.cmd", "unsubscribed");
      dict_add(unsub, "media.chan-uuid", cp->uuid);
      dict_add_ulong(unsub, "media.ts", now);
      ws_send_dict(NULL, cptr, unsub, WEBSOCKET_OP_TEXT);
      dict_free(unsub);
      char description[512];
      Log(LOG_DEBUG, "ws.media", "Unsubscribed %s from channel %s", cptr->chatname, media_chan_description(cp, NULL, description, sizeof(description)));
      media_send_available_all(NULL);

      return true;
   } else if (strcasecmp(media_cmd, "codec") == 0) {
      // Codec selection is per concrete channel UUID. This matters for rigs
      // with multiple independent VFO streams and also gives the fwdsp manager
      // enough identity to switch one channel without disturbing another.
      const char *codec = dict_get(d, "media.codec", NULL);
      struct rr_mediachan *cp = (uuid ? media_chan_find_uuid(uuid) : NULL);

      if (cp && !media_client_in_channel_room(cptr, cp)) {
         ws_send_error(cptr, "Join room %s before selecting its codec", cp->room);

         return false;
      }

      bool codec_authorized = cptr->authenticated && cptr->user && has_priv(cptr->user->uid, cp && cp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx");

      if (cp && !codec_authorized && cptr->authenticated && cptr->user) {
         /* The application owns remote-resource authorization. The protocol asks through the synchronous event boundary; default deny. */
         rr_media_access_t request = {
            .channel = cp, .allowed = false
         };
         event_emit_binary(RR_MEDIA_CODEC_PRIVILEGE_EVENT, cptr, &request, sizeof(request));
         codec_authorized = request.allowed;
      }

      if (cp && !codec_authorized) {
         ws_send_error(cptr,
            "Codec selection denied: channel %s (%s), room %s, direction %s; requires a local %s account privilege or a destination-side resource grant", cp->
            name, cp->uuid, cp->room, cp->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX", cp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx");

         return false;
      }

      if (!media_codec_valid(codec)) {
         ws_send_error(cptr, "media.codec select: invalid codec %s for channel %s", codec ? codec : "(missing)", uuid ? uuid : "(missing)");

         return false;
      }

      if (!cp) {
         ws_send_error(cptr, "media.codec select: unknown or missing channel UUID %s; use /media list", uuid ? uuid : "(missing)");

         return false;
      }

      if (cp->subsystem == RR_BINFRAME_SUBSYS_VIDEO) {
         ws_send_error(cptr, "Use video profile selection for this channel");

         return false;
      }

      if (cp->subsystem == RR_BINFRAME_SUBSYS_MODEM && (!strcmp(cp->codec, "nmea") || !strcmp(cp->codec, "gpsp"))) {
         ws_send_error(cptr, "GPS media channel %s (%s) has fixed codec %s; cannot select %s", cp->name, cp->uuid, cp->codec, codec);

         return false;
      }

      if (cp->direction == RR_BINFRAME_DIR_TX && codec_is_test_variant(codec)) {
         ws_send_error(cptr, "Cannot select test codec %s on TX media channel %s (%s); test codecs require an RX channel", codec, cp->name, cp->uuid);

         return false;
      }

      if (!media_server_supports_codec(codec)) {
         ws_send_error(cptr, "Server does not support codec %s for media channel %s (%s)", codec, cp->name, cp->uuid);

         return false;
      }

      if (!media_client_supports_codec(cptr, codec) || !media_channel_all_clients_support(cp, codec)) {
         ws_send_error(cptr, "Cannot select codec %s for media channel %s (%s): at least one subscriber does not support it", codec, cp->name, cp->uuid);

         return false;
      }

      char old_codec[5] = {
         0
      };

      if (cp->codec[0] != '\0') {
         memcpy(old_codec, cp->codec, 4);
      }

      if (old_codec[0] && memcmp(old_codec, codec, 4) == 0) {
         media_send_available(cptr, cp);

         return true;
      }

      /* The encoder belongs to the shared channel, not the requesting client. Never change its wire format while another listener is attached. */
      if (cp->subsystem == RR_BINFRAME_SUBSYS_AUDIO && old_codec[0]) {
         u_int32_t chan_id = (u_int32_t) (cp - media_channels) + 1;

         for (rrconn_t *peer = http_client_list ; peer ; peer = peer->next) {
            if (peer == cptr || !media_client_in_channel_room(peer, cp)) {
               continue;
            }
            bool subscribed = cp->direction == RR_BINFRAME_DIR_TX ?
               chan_id_in_array(peer->tx_channels, MAX_TX_CHANNELS, chan_id) : chan_id_in_array(peer->rx_channels, MAX_RX_CHANNELS, chan_id);

            if (subscribed) {
               ws_send_error(cptr, "Codec %s is already shared by other listeners on channel %s (%s); cannot switch to %s while they are subscribed", old_codec,
                  cp->name, cp->uuid, codec);

               return false;
            }
         }
      }

      dict *sel = dict_new();

      if (sel) {
         dict_add(sel, "media.codec", codec);
         dict_add_ulong(sel, "media.dir", cp->direction);
         dict_add(sel, "media.chan-uuid", cp->uuid);

         if (old_codec[0] != '\0') {
            dict_add(sel, "media.old-codec", old_codec);
         }
         event_emit_dict("media.codec-select", cptr, sel);
         dict_free(sel);
      }
      // The event handler starts the replacement pipeline synchronously. Once
      // it returns, publish the selected codec on the channel and re-announce
      // it so a waiting client can subscribe with the confirmed codec.
      snprintf(cp->codec, sizeof(cp->codec), "%s", codec);

      if (cp->direction == RR_BINFRAME_DIR_TX) {
         snprintf(cptr->codec_tx, sizeof(cptr->codec_tx), "%s", codec);
      } else if (cp->direction == RR_BINFRAME_DIR_RX) {
         snprintf(cptr->codec_rx, sizeof(cptr->codec_rx), "%s", codec);
      }
      media_send_available(cptr, cp);

      dict *ack = dict_new();

      if (ack) {
         dict_add(ack, "msg.type", "media");
         dict_add(ack, "media.cmd", "isupport");
         dict_add(ack, "media.codecs", codec);
         dict_add(ack, "media.preferred", codec);
         dict_add(ack, "media.chan-uuid", cp->uuid);
         dict_add_ulong(ack, "media.dir", cp->direction);
         dict_add_ulong(ack, "media.ts", now);
         ws_send_dict(NULL, cptr, ack, WEBSOCKET_OP_TEXT);
         dict_free(ack);
      }
      char description[512];
      Log(LOG_INFO, "ws.media", "%s selected codec %s for %s", cptr->chatname, codec, media_chan_description(cp, NULL, description, sizeof(description)));

      return true;
   }

   Log(LOG_DEBUG, "ws.media", "Unhandled media cmd: |%s|", media_cmd);

   return false;
}

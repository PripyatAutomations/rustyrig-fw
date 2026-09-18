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
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>

extern time_t now;

#ifndef MAX_MEDIA_CHANNELS
#define	MAX_MEDIA_CHANNELS 64
#endif

// The registry; the program (rrserver) fills this in via media_chan_add()
struct rr_mediachan media_channels[MAX_MEDIA_CHANNELS];

// Fill in a uuid for a new channel: use a random-ish but stable string
static void media_gen_uuid(char *out, size_t len) {
   static uint64_t counter = 0;

   snprintf(out, len, "%llx-%04llx", (unsigned long long)now,
      (unsigned long long)((uintptr_t)&media_channels[counter] + ++counter));
}

struct rr_mediachan *media_chan_add(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig, const char *codec, const char *descr) {
   // Already have this routing quadruple?
   struct rr_mediachan *cp = media_chan_find(subsystem, direction, vfo, rig);

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

      if (codec) {
         snprintf(cp->codec, sizeof(cp->codec), "%s", codec);
      }
      if (descr) {
         snprintf(cp->descr, sizeof(cp->descr), "%s", descr);
      }
      Log(LOG_DEBUG, "ws.media", "Added media channel %s: subsys 0x%02X dir %s vfo %u rig %u (%s)",
         cp->uuid, subsystem, (direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), vfo, rig,
         (descr ? descr : "-"));

      return cp;
   }
   Log(LOG_CRIT, "ws.media", "media_chan_add: channel table full!");

   return NULL;
}

struct rr_mediachan *media_chan_find(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig) {
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *cp = &media_channels[i];

      if (cp->uuid[0] == '\0') {
         continue;
      }
      if (cp->subsystem == subsystem && cp->direction == direction &&
          cp->vfo == vfo && cp->rig == rig) {
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
      if (media_channels[i].uuid[0] != '\0' &&
          strcasecmp(media_channels[i].uuid, uuid) == 0) {
         return &media_channels[i];
      }
   }
   return NULL;
}

void media_channels_free(void) {
   memset(media_channels, 0, sizeof(media_channels) );
}

// Remove a channel by uuid; returns false on OK. Does not notify clients -
// pair with media_send_chan_removed_all() when the removal is user-visible.
bool media_chan_remove(const char *uuid) {
   struct rr_mediachan *cp = media_chan_find_uuid(uuid);

   if (!cp) {
      return true;
   }
   Log(LOG_INFO, "ws.media", "Removed media channel %s (subsys 0x%02X dir %s vfo %u rig %u)",
      cp->uuid, cp->subsystem, (cp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"),
      cp->vfo, cp->rig);
   memset(cp, 0, sizeof(*cp) );

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
   rrconn_t *cur = http_client_list;

   while (cur) {
      media_send_chan_removed(cur, cp);
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
   dict_add_ulong(d, "media.ts", now);

   if (cp->codec[0] != '\0') {
      dict_add(d, "media.codec", cp->codec);
   }
   if (cp->descr[0] != '\0') {
      dict_add(d, "media.descr", cp->descr);
   }
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool media_send_available_all(rrconn_t *cptr) {
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
   if (chan_in_array(arr, max, chan_id) ) {
      return false;   // already subscribed
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

// Per-channel central sequence number for fan-out frames (wraps)
static uint32_t media_seq = 0;

// Fan out one media payload to every connection subscribed to channel `cp`.
// The server owns the wire header values (see doc/media-frames.md).
bool ws_media_broadcast_subscribed(struct rr_mediachan *cp, const uint8_t *payload,
   size_t len, const char codec[4]) {
   if (!cp || cp->uuid[0] == '\0' || !payload || len > RR_BINFRAME_MAX_PAYLOAD) {
      return true;
   }
   u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;
   char codecbuf[4] = { 0 };

   if (codec && codec[0] != '\0') {
      memcpy(codecbuf, codec, 4);
   } else if (cp->codec[0] != '\0') {
      memcpy(codecbuf, cp->codec, 4);
   }
   uint8_t *frame = NULL;
   int flen = rr_binframe_frame(&frame, cp->subsystem, codecbuf,
      cp->direction, cp->vfo, cp->rig, (uint8_t)(chan_id & 0xFF),
      ++media_seq, mono_us(), payload, len);

   if (flen < 0) {
      return true;
   }
   rrconn_t *cur = http_client_list;

   while (cur) {
      if (cur->is_ws && cur->authenticated && cur->conn &&
               ((cp->direction == RR_BINFRAME_DIR_TX &&
                  chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id)) ||
                (cp->direction == RR_BINFRAME_DIR_RX &&
                  chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id))) ) {
         mg_ws_send(cur->conn, frame, flen, WEBSOCKET_OP_BINARY);
      }
      cur = cur->next;
   }
   free(frame);

   return false;
}
bool ws_handle_mediachan_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return true;
   }
   const char *media_cmd = dict_get(d, "media.cmd", NULL);
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);

   if (!media_cmd) {
      Log(LOG_DEBUG, "ws.media", "media message without media.cmd");
      return true;
   }

   if (strcasecmp(media_cmd, "source") == 0) {
      // Media source registration (rrmedia, fwdsp feeds, remote relays).
      // Must be authenticated, and the account must carry the media.source
      // priv (checked at auth time -> FLAG_MEDIA_SOURCE). The source tells
      // us which channel(s) it will feed; the server confirms per channel.
      if (!cptr->authenticated ||
          !(client_has_flag(cptr, FLAG_MEDIA_SOURCE) || client_has_flag(cptr, FLAG_VIDEO_SOURCE) ) ) {
         Log(LOG_AUDIT, "auth", "Denied media.source from %s on cptr:<%p> (no media.source/video-source priv or unauthenticated)",
            (cptr->chatname[0] != '\0' ? cptr->chatname : "(unknown)"), cptr);
         ws_send_error(cptr, "Not authorized as a media source");

         return true;
      }
      // No uuid = register as a source for all channels (like media.available
      // suggests); with a uuid, register for that one channel.
      client_set_flag(cptr, FLAG_MEDIA_SOURCE);   // redundant; belt & braces
      cptr->connection_type = CONN_AUDIO_RX;      // legacy marker: feeds media

      if (uuid && uuid[0] != '\0') {
         struct rr_mediachan *cp = media_chan_find_uuid(uuid);

         if (!cp) {
            ws_send_error(cptr, "No such media channel");

            return true;
         }
         // A source subscribes to its feed channel in the push direction
         u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;
         bool oom = (cp->direction == RR_BINFRAME_DIR_TX ?
            chan_add_to_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) :
            chan_add_to_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id) );

         if (oom) {
            ws_send_error(cptr, "Too many media subscriptions");

            return true;
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
      Log(LOG_INFO, "ws.media", "Media source registered: %s (cptr:<%p>, channel %s)",
         cptr->chatname, cptr, (uuid && uuid[0] ? uuid : "<all>"));

      // Let the program (rrserver) know a source joined, e.g. to hook the
      // fwdsp pipeline up to this connection.
      event_emit_dict("media.source", cptr, d);

      return false;
   } else if (strcasecmp(media_cmd, "list") == 0) {
      // Client wants the (possibly updated) channel list
      media_send_available_all(cptr);

      return false;
   } else if (strcasecmp(media_cmd, "subscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      // PARITY: rustyrig-www/js/webui.media.js (subscribeMediaChannel)
      // A subscribe without a (known) uuid is a channel creation request:
      // the client tells us what to make via media.subsys/dir/vfo/rig (the
      // routing quadruple), we generate the uuid per the existing logic in
      // media_chan_add(). Defaults: RX audio on the first rig.
      if (!cp) {
         uint32_t subsys = dict_get_ulong(d, "media.subsys", RR_BINFRAME_SUBSYS_AUDIO);
         uint32_t dir = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_RX);
         uint32_t vfo = dict_get_ulong(d, "media.vfo", 0);
         uint32_t rig = dict_get_ulong(d, "media.rig", 0);
         const char *descr = dict_get(d, "media.descr", NULL);
         const char *codec = dict_get(d, "media.codec", NULL);

         cp = media_chan_add( (uint8_t)subsys, (uint8_t)dir, (uint8_t)vfo,
            (uint8_t)rig, codec, descr);

         if (!cp) {
            Log(LOG_WARN, "ws.media", "Subscribe-create failed for %s (table full?)", cptr->chatname);
            ws_send_error(cptr, "No such media channel");

            return true;
         }
         // Tell everyone (including the requester) about the new channel
         media_send_available_all(cptr);
      }
      // Channel id is 1 + table index; 0 means "no channel" in the
      // rx_channels/tx_channels arrays
      u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;
      bool is_tx = (cp->direction == RR_BINFRAME_DIR_TX);
      bool oom = (is_tx ?
         chan_add_to_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) :
         chan_add_to_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id) );

      if (oom) {
         Log(LOG_WARN, "ws.media", "No free channel slots for %s subscribing to %s",
            cptr->chatname, cp->uuid);
         ws_send_error(cptr, "Too many media subscriptions");

         return true;
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
      dict_free(sub);
      Log(LOG_DEBUG, "ws.media", "Subscribed %s to channel %s (stream %u)", cptr->chatname, cp->uuid, chan_id);

      return false;
   } else if (strcasecmp(media_cmd, "unsubscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      if (!cp) {
         ws_send_error(cptr, "No such media channel");
         return true;
      }
      u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;

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
      Log(LOG_DEBUG, "ws.media", "Unsubscribed %s from channel %s", cptr->chatname, cp->uuid);

      return false;
   } else if (strcasecmp(media_cmd, "codec") == 0) {
      // Codec selection is per concrete channel UUID. This matters for rigs
      // with multiple independent VFO streams and also gives the fwdsp manager
      // enough identity to switch one channel without disturbing another.
      const char *codec = dict_get(d, "media.codec", NULL);
      struct rr_mediachan *cp = (uuid ? media_chan_find_uuid(uuid) : NULL);

      if (!codec || strlen(codec) != 4) {
         ws_send_error(cptr, "media.codec select: invalid codec");
         return true;
      }
      if (!cp) {
         ws_send_error(cptr, "media.codec select: unknown or missing channel uuid");
         return true;
      }

      char old_codec[5] = { 0 };
      if (cp->codec[0] != '\0') {
         memcpy(old_codec, cp->codec, 4);
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
      Log(LOG_INFO, "ws.media", "%s selected codec %s for %s",
         cptr->chatname, codec, cp->uuid);

      return false;
   }
   Log(LOG_DEBUG, "ws.media", "Unhandled media cmd: |%s|", media_cmd);

   return true;
}

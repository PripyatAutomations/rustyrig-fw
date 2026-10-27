//
// rrclient/media.c: client side media channel handling
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// When the server announces a media.available channel, we remember it and
// auto-subscribe to the RX and TX audio channels for the active VFO once
// both directions appear (or immediately when we already have them). We do
// NOT assume the rig has only one VFO: rigs like the Radioberry expose
// multiple independent RX VFOs, and the user can switch channels later.
//
// PARITY: rustyrig-www/js/webui.media.js
//
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <glib.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <rrclient/media.direct.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
#include <librustyaxe/io.serial.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrclient/vfo.h>
#include <rrclient/audio.h>
#include <rrclient/media.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <rrclient/connman.h>
#include <rrclient/rooms.h>
#include <rrclient/resource.context.h>
#include <rrclient/objects.h>

extern rrconn_t *ws_conn;
extern time_t now;
extern const char *login_user;
extern bool ui_print(const char *window, const char *fmt, ...);
#include <rrclient/frontend.h>

/* Media command replies belong with the conversation that requested them. Falling back to NULL sends them to the TUI status window, which makes a
 * /media LIST in a room unexpectedly pollute the client log/status tab. */
static const char *media_output_window(void) {
   return ui_active_window_name();
}

#define media_print(...) ui_print(media_output_window(), __VA_ARGS__)

// Privileges the server granted us at auth (e.g. "admin,edit,view,...")
static char media_my_privs[128] = {
   0
};

// Do we hold any of the given '|'-separated privileges? Uses the protocol
// library's comma-list matcher against the privs the server sent us.
bool media_have_priv(const char *priv) {
   if (!priv || priv[0] == '\0') {
      return false;
   }
   const char *p = priv;

   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);
      char tmp[64];

      if (len >= sizeof(tmp)) {
         len = sizeof(tmp) - 1;
      }
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (match_priv(media_my_privs, tmp)) {
         return true;
      }
      p = sep ? sep + 1 : NULL;
   }
   return false;
}

#define RR_MEDIA_MAX_CHANS RR_CLIENT_MEDIA_MAX_CHANS

static struct rr_client_media_chan known_chans[RR_MEDIA_MAX_CHANS];
static char media_room[128];
static char gps_scopes[1024];
static bool gps_wanted(const struct rr_client_media_chan *channel);
typedef struct {
   bool known;
   int32_t lat, lon;
   uint8_t flags;
   char raw_nmea[512];
} gps_position_cache_t;
static gps_position_cache_t gps_position_cache[RR_MEDIA_MAX_CHANS];
static guint gps_nmea_timer;

static void gps_nmea_tick_connection(void *unused) {
   (void)unused;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];
      gps_position_cache_t *cached = &gps_position_cache[i];

      if (!cached->known || !channel->joined || !channel->subscribed ||
         !channel->stream_valid || strcmp(channel->codec, RR_GPS_FRAME_CODEC) || !gps_wanted(channel)) {
         continue;
      }
      char sentence[128];

      if (!rr_gps_nmea_rmc(cached->lat, cached->lon, cached->flags, time(NULL), sentence, sizeof(sentence))) {
         continue;
      }
      const char *suffix = strstr(channel->name, ".gps.rx");

      if (!suffix) {
         continue;
      }
      char source[64];
      size_t n = (size_t)(suffix - channel->name);

      if (n >= sizeof(source)) {
         continue;
      }
      memcpy(source, channel->name, n);
      source[n] = 0;
      dict *d = dict_new();

      if (!d) {
         continue;
      }
      dict_add(d, "gps.channel-uuid", channel->uuid);
      dict_add(d, "gps.control-room", channel->control_room);
      dict_add(d, "gps.source", source);
      dict_add(d, "gps.nmea", sentence);
      dict_add_bool(d, "gps.raw", false);
      dict_add_bool(d, "gps.selected", ws_room_same_rig(media_room, channel->control_room) || (!strcmp(source, "station") && !rrclient_room_vfo_mask(media_room)
         ));
      event_emit_dict("serial.gps.output", NULL, d);
      dict_free(d);
   }
}
static gboolean gps_nmea_tick(gpointer unused) {
   (void)unused;

   /* The active connection may be IRC while a different server continues to provide the rig's GPS. Process each server's own cached streams. */
   for (unsigned i = 0 ;; i++) {
      const char *profile = rrclient_connection_iter(i);

      if (!profile) {
         break;
      }
      rrconn_t *conn = rrclient_connection_find(profile);

      if (conn && conn->is_ws) {
         rrclient_connection_dispatch(conn, gps_nmea_tick_connection, NULL);
      }
   }

   return G_SOURCE_CONTINUE;
}

/* Re-announce the selected transmitter site's cached position immediately when ttyGPS changes rig, without waiting for a fresh GPS frame. */
void rrclient_media_emit_gps_for_room(const char *room) {
   if (!room || !*room) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *ch = &known_chans[i];
      const gps_position_cache_t *cached = &gps_position_cache[i];

      if (!ch->subscribed || !ch->stream_valid || !ws_room_same_rig(room, ch->control_room)) {
         continue;
      }
      char sentence[512], source[64];
      bool raw = !strcmp(ch->codec, RR_NMEA_FRAME_CODEC);

      if (raw) {
         if (!cached->raw_nmea[0]) {
            continue;
         }
         snprintf(sentence, sizeof(sentence), "%s", cached->raw_nmea);
      } else if (!strcmp(ch->codec, RR_GPS_FRAME_CODEC) && cached->known) {
         if (!rr_gps_nmea_rmc(cached->lat, cached->lon, cached->flags, time(NULL), sentence, sizeof(sentence))) {
            continue;
         }
      } else {
         continue;
      }
      const char *suffix = strstr(ch->name, raw ? ".nmea.rx" : ".gps.rx");

      if (!suffix || (size_t)(suffix - ch->name) >= sizeof(source)) {
         continue;
      }
      size_t n = (size_t)(suffix - ch->name);
      memcpy(source, ch->name, n);
      source[n] = 0;
      dict *d = dict_new();

      if (!d) {
         continue;
      }
      dict_add(d, "gps.channel-uuid", ch->uuid);
      dict_add(d, "gps.control-room", ch->control_room);
      dict_add(d, "gps.source", source);
      dict_add(d, "gps.nmea", sentence);
      dict_add_bool(d, "gps.raw", raw);
      dict_add_bool(d, "gps.selected", true);
      event_emit_dict("serial.gps.output", NULL, d);
      dict_free(d);

      return;                   /* only one initial location announcement */
   }
}

static bool media_ready = false; // have we got the first available batch?
static bool direction_disabled[2];
const struct rr_client_media_chan *rrclient_media_chan_lookup(const char *arg);

static struct rr_client_media_chan *media_known_find(const char *uuid) {
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] != '\0' && strcasecmp(known_chans[i].uuid, uuid) == 0) {
         return &known_chans[i];
      }
   }

   return NULL;
}

static struct rr_client_media_chan *media_known_add(const char *uuid) {
   struct rr_client_media_chan *kp = media_known_find(uuid);

   if (kp) {
      return kp;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] == '\0') {
         kp = &known_chans[i];
         memset(kp, 0, sizeof(*kp));
         snprintf(kp->uuid, sizeof(kp->uuid), "%s", uuid);

         return kp;
      }
   }

   return NULL;
}

// Prefer the exact active-VFO channel for playback and microphone routing;
// a VFO from another channel must never become the TX target just because it
// happens to remain subscribed while the operator changes VFOs.
static struct rr_client_media_chan *media_current_channel(bool is_tx) {
   uint8_t direction = is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX;
   char vfo = vfo_state_get_active();
   struct rr_client_media_chan *wildcard = NULL;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];

      if (!kp->uuid[0] || !kp->subscribed || kp->disabled ||
         kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO || kp->direction != direction ||
         !kp->codec[0] || (kp->room[0] && (!kp->joined || strcasecmp(kp->room, media_room)))) {
         continue;
      }

      if (kp->vfo == (uint8_t) (vfo - 'A')) {
         return kp;
      }

      if (kp->vfo == RR_BINFRAME_VFO_NA && !wildcard) {
         wildcard = kp;
      }
   }

   return wildcard;
}

/* Return the channel the UI should address for a codec request.  Unlike the audio pipeline selector above, this deliberately includes a channel that is
 * disabled (NONE) or waiting for a subscription confirmation.  Otherwise selecting a codec after NONE loses the UUID and sends a misleading direction-wide
 * request. */
static struct rr_client_media_chan *media_codec_target_channel(bool is_tx) {
   uint8_t direction = is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX;
   char active = vfo_state_get_active();
   struct rr_client_media_chan *fallback = NULL;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];

      if (!kp->uuid[0] || kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
         kp->direction != direction || (kp->room[0] && (!kp->joined || strcasecmp(kp->room, media_room)))) {
         continue;
      }

      if (kp->vfo == RR_BINFRAME_VFO_NA && !fallback) {
         fallback = kp;
      }

      if (kp->vfo == (uint8_t) (active - 'A')) {
         if (kp->subscribed && !kp->disabled) {
            return kp;
         }

         if (!fallback) {
            fallback = kp;
         }
      }
   }

   return fallback;
}

const struct rr_client_media_chan *rrclient_media_current_channel(bool is_tx) {
   return media_current_channel(is_tx);
}

const struct rr_client_media_chan *rrclient_media_codec_target_channel(bool is_tx) {
   return media_codec_target_channel(is_tx);
}

const char *rrclient_media_current_codec(bool is_tx) {
   struct rr_client_media_chan *channel = media_current_channel(is_tx);

   return channel ? channel->codec : NULL;
}

// Resolve a subscribed RX audio channel by its wire stream id and codec.
// Returns the negotiated codec when the frame belongs to a channel we are
// subscribed to, or NULL when the frame is stale (old stream after a codec
// switch) or for a channel we no longer follow. Consumers must route by
// this instead of the mutable current-codec state so a codec switch cannot
// cross-feed frames between decoders.
static struct rr_client_media_chan *media_rx_for_stream(uint8_t stream, const char codec[4]) {
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (channel->uuid[0] && channel->subscribed && !channel->disabled && (!channel->room[0] || channel->joined) && channel->stream_valid &&
         channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO && channel->direction == RR_BINFRAME_DIR_RX &&
         channel->stream == stream && !strncmp(channel->codec, codec, 4)) {
         return channel;
      }
   }

   return NULL;
}
const char *rrclient_media_rx_codec_for_stream(uint8_t stream, const char codec[4]) {
   struct rr_client_media_chan *channel = media_rx_for_stream(stream, codec);

   return channel ? channel->codec : NULL;
}
const char *rrclient_media_rx_uuid_for_stream(uint8_t stream, const char codec[4]) {
   struct rr_client_media_chan *channel = media_rx_for_stream(stream, codec);

   return channel ? channel->uuid : NULL;
}

/* PARITY: rustyrig-www/js/webui.audio.js webui_mute_talkback. */
bool rrclient_media_local_tx(const char *uuid) {
   extern const char *login_user;
   extern struct rr_user *global_userlist;

   if (!login_user || !uuid) {
      return false;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *channel = &known_chans[i];

      if (strcmp(channel->uuid, uuid)) {
         continue;
      }

      for (const struct rr_user *user = global_userlist ; user ; user = user->next) {
         const char *room = user->ptt_room[0] ? user->ptt_room : user->room;
         const char *control = channel->control_room[0] ? channel->control_room : channel->room;

         if (user->is_ptt && !strcasecmp(user->name, login_user) && room[0] &&
            (!strcasecmp(room, control) || ws_room_same_rig(room, control)) && (channel->vfo == RR_BINFRAME_VFO_NA || user->ptt_vfo == 'A' + channel->vfo)) {
            return true;
         }
      }
   }

   return false;
}

void rrclient_media_sync_audio(void) {
   struct rrclient_rx_audio channels[RR_MEDIA_MAX_CHANS];
   size_t count = 0;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (channel->uuid[0] && channel->subscribed && !channel->disabled && channel->codec[0] &&
         (!channel->room[0] || channel->joined) && channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO && channel->direction == RR_BINFRAME_DIR_RX) {
         channels[count++] = (struct rrclient_rx_audio) {
            channel->uuid, channel->codec, channel->room
         };
      }
   }

   audio_sync_rx_channels(channels, count);

   if (!rrclient_present_context()) {
      return;
   }
   const char *codec = rrclient_media_current_codec(true);

   if (codec) {
      audio_switch_codec(codec, true);
   } else {
      audio_stop_codec(true);
   }
   event_emit("client.media.changed", ws_conn, "");
}

static const char *media_codec_supported(const char *codec) {
   const char *list = media_get_common_codecs();

   if (!list || !codec || strlen(codec) != 4) {
      return NULL;
   }
   while (*list) {
      while (*list == ' ') {
         list++;
      }
      const char *end = strchr(list, ' ');
      size_t len = end ? (size_t)(end - list) : strlen(list);

      if (len == 4 && strncasecmp(codec, list, 4) == 0) {
         return list;
      }

      if (!end) {
         break;
      }
      list = end + 1;
   }
   return NULL;
}

// NONE is local subscription intent, never an encoded format on the wire.
// The default applies to all subscribed (or explicitly disabled) audio channels.
static bool media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec, const char *target) {
   if (!cptr || !media_ready || !codec || strlen(codec) != 4) {
      return true;
   }
   bool none = strcasecmp(codec, "none") == 0;
   const char *canonical = media_codec_supported(codec);

   if (!none && !canonical) {
      media_print("Codec %s is not in the negotiated codec list", codec);

      return true;
   }
   const struct rr_client_media_chan *selected = target ? rrclient_media_chan_lookup(target) : NULL;

   if (!selected) {
      media_print("No such media channel: %s", target);

      return true;
   }
   char normalized[5] = {
      0
   };

   if (canonical) {
      // User input is case-insensitive; preserve the negotiated wire ID (e.g. opuT).
      memcpy(normalized, canonical, 4);
   }
   uint8_t direction = is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX;
   bool sent = false, failed = false, unchanged = false;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];

      if (!kp->uuid[0] || kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
         kp->direction != direction || (selected && kp != selected) || (!kp->subscribed && !kp->disabled)) {
         continue;
      }

      /* One channel per command. Do not reconfigure an existing shared
       * media stream when the requested codec is already active. */
      if ((none && kp->disabled) ||
          (!none && !kp->disabled && !kp->pending_codec[0] && !strcasecmp(kp->codec, normalized))) {
         unchanged = true;
         continue;
      }
      if (none) {
         if (kp->subscribed && !media_send_unsubscribe(cptr, kp->uuid)) {
            failed = true;
            continue;
         }
         kp->disabled = true;
         kp->subscribed = false;
         kp->pending_codec[0] = '\0';
      } else {
         if (!media_send_codec_select(cptr, normalized, kp->uuid)) {
            failed = true;
            continue;
         }

         if (kp->disabled) {
            memcpy(kp->pending_codec, normalized, sizeof(kp->pending_codec));
         }
      }
      char description[512];
      rrclient_media_description(kp->uuid, description, sizeof(description));
      Log(LOG_INFO, "ws.media", "Requested %s codec %s for %s", is_tx ? "TX" : "RX", normalized, description);
      sent = true;
   }

   // Keep the current local pipeline until the server re-announces the
   // channel with the requested codec. NONE is local unsubscribe intent and
   // can stop audio immediately; encoded codec changes wait for confirmation.
   if (none) {
      rrclient_media_sync_audio();
   }

   if (!sent && !unchanged) {
      media_print("No subscribed %s audio channels match; use /media LIST", is_tx ? "TX" : "RX");
   }

   return failed || (!sent && !unchanged);
}

bool rrclient_media_select_codec_channel(rrconn_t *cptr, bool is_tx, const char *codec, const char *uuid) {
   if (!uuid || !*uuid) {
      return true;
   }
   return media_select_codec(cptr, is_tx, codec, uuid);
}

bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec) {
   const struct rr_client_media_chan *target = media_codec_target_channel(is_tx);
   return target ? media_select_codec(cptr, is_tx, codec, target->uuid) : true;
}

/* A source exposing GPSP and NMEA needs only GPSP on the wire. Compare the shared source prefix and controlling rig, not VFO/tab state. */
static bool gpsp_counterpart(const struct rr_client_media_chan *nmea) {
   const char *suffix = strstr(nmea->name, ".nmea.rx");

   if (!suffix) {
      return false;
   }
   size_t prefix = (size_t)(suffix - nmea->name);

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *candidate = &known_chans[i];

      if (!candidate->uuid[0] || strcmp(candidate->codec, RR_GPS_FRAME_CODEC) ||
         candidate->direction != RR_BINFRAME_DIR_RX || strcasecmp(candidate->control_room, nmea->control_room)) {
         continue;
      }
      const char *end = strstr(candidate->name, ".gps.rx");

      if (end && (size_t)(end - candidate->name) == prefix && !strncmp(candidate->name, nmea->name, prefix)) {
         return true;
      }
   }

   return false;
}
static bool gps_nmea_explicit(const struct rr_client_media_chan *channel) {
   const char *suffix = strstr(channel->name, ".nmea.rx");

   if (!suffix) {
      return false;
   }
   char source[64];
   size_t len = (size_t)(suffix - channel->name);

   if (len >= sizeof(source)) {
      return false;
   }
   memcpy(source, channel->name, len);
   source[len] = 0;
   char scopes[sizeof(gps_scopes)];
   snprintf(scopes, sizeof(scopes), "%s", gps_scopes);
   char *save = NULL;

   for (char *token = strtok_r(scopes, " ", &save) ; token ; token = strtok_r(NULL, " ", &save)) {
      if (!strncmp(token, "nmea:", 5) && (!strcmp(token + 5, source) || !strcmp(token + 5, channel->uuid))) {
         return true;
      }
   }

   return false;
}
static void gps_prefer_packed(rrconn_t *connection) {
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (strcmp(channel->codec, RR_NMEA_FRAME_CODEC) ||
         !channel->automatic || !channel->subscribed || gps_nmea_explicit(channel) || !gpsp_counterpart(channel)) {
         continue;
      }

      if (connection && media_send_unsubscribe(connection, channel->uuid)) {
         channel->subscribed = false;
         channel->automatic = false;
         channel->stream_valid = false;
         Log(LOG_DEBUG, "media.gps", "Using GPSP instead of NMEA for %s", channel->name);
      }
   }
}

// Track pending subscriptions so we only subscribe once per channel
static bool gps_wanted(const struct rr_client_media_chan *channel) {
   if (channel->subsystem != RR_BINFRAME_SUBSYS_MODEM || channel->direction != RR_BINFRAME_DIR_RX ||
      (strcmp(channel->codec, RR_GPS_FRAME_CODEC) && strcmp(channel->codec, RR_NMEA_FRAME_CODEC)) || !channel->joined) {
      return false;
   }
   bool raw = !strcmp(channel->codec, RR_NMEA_FRAME_CODEC);

   if (raw && gpsp_counterpart(channel) && !gps_nmea_explicit(channel)) {
      return false;
   }
   const char *suffix = strstr(channel->name, raw ? ".nmea.rx" : ".gps.rx");

   if (!suffix) {
      return false;
   }
   char source[64];
   size_t len = suffix - channel->name;

   if (len >= sizeof(source)) {
      return false;
   }
   memcpy(source, channel->name, len);
   source[len] = '\0';

   /* A joined rig room implicitly requests any advertised GPS/NMEA RX channels for that rig. Output selection is still handled separately by serial.gps.output,
    * so ttyGPS follows the active rig. */
   if (channel->room[0] && rrclient_room_is_joined(channel->room) && rrclient_room_vfo_mask(channel->room)) {
      return true;
   }

   char scopes[sizeof(gps_scopes)];
   snprintf(scopes, sizeof(scopes), "%s", gps_scopes);
   char *save = NULL;

   for (char *scope = strtok_r(scopes, " ", &save) ; scope ; scope = strtok_r(NULL, " ", &save)) {
      bool requested_raw = !strncmp(scope, "nmea:", 5);

      if (requested_raw != raw) {
         continue;
      }

      if (requested_raw) {
         scope += 5;
      }

      if (!strcmp(scope, source) || !strcmp(scope, channel->uuid)) {
         return true;
      }

      if (!strcmp(scope, "active") &&
         (ws_room_same_rig(media_room, channel->control_room) || (!strcmp(source, "station") && !rrclient_room_vfo_mask(media_room)))) {
         return true;
      }
   }

   return false;
}
static void media_try_autosubscribe(rrconn_t *cptr, struct rr_client_media_chan *kp) {
   if (!cptr || !kp || !media_ready || kp->subscribed || kp->disabled || rrclient_room_offline(kp->room)) {
      return;
   }

   if (gps_wanted(kp)) {
      if (media_send_subscribe(cptr, kp->uuid)) {
         kp->subscribed = true;
         kp->automatic = true;
      }

      return;
   }

   // PARITY: rustyrig-www/js/webui.media.js mediaTryAutosubscribe.
   // Auto audio follows the selected joined rig room; site chat has no media.
   if (kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
      (kp->vfo != RR_BINFRAME_VFO_NA && kp->vfo != (uint8_t) (vfo_state_get_active() - 'A')) ||
      (kp->room[0] && (!kp->joined || strcasecmp(kp->room, media_room)))) {
      return;
   }

   /* An RX codec rendition is an alternative encoding of the same rig/VFO
    * PCM source. Automatic selection must not subscribe every rendition. */
   if (kp->direction == RR_BINFRAME_DIR_RX) {
      for (int i = 0; i < RR_MEDIA_MAX_CHANS; i++) {
         const struct rr_client_media_chan *other = &known_chans[i];
         if (other != kp && other->uuid[0] && other->subscribed &&
            other->subsystem == kp->subsystem &&
            other->direction == kp->direction && other->vfo == kp->vfo &&
            other->rig == kp->rig &&
            !strcmp(other->rig_uuid, kp->rig_uuid) &&
            !strcmp(other->vfo_uuid, kp->vfo_uuid)) {
            return;
         }
      }
   }

   if (direction_disabled[kp->direction == RR_BINFRAME_DIR_TX]) {
      kp->disabled = true;

      return;
   }

   if (media_send_subscribe(cptr, kp->uuid)) {
      kp->subscribed = true;
      kp->automatic = true;
   }
}

static void gps_outputs_changed(const char *event, const char *data, rrconn_t *client, void *user) {
   snprintf(gps_scopes, sizeof(gps_scopes), "%s", data ? data : "");

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];

      if (gps_wanted(kp)) {
         media_try_autosubscribe(client, kp);
      }
      /* GPS/NMEA subscriptions are site metadata. Changing which local PTY consumes them must never tear down an existing remote subscription. */
   }
}

// Called by events.c with the already-parsed media.available dict (no JSON
// round-trip: dotted keys don't survive dict2json -> json2dict reliably).
// Keep server-originated subscriptions on their owning connection.
void rrclient_media_available(dict *d, rrconn_t *cptr) {
   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   uint32_t subsys = dict_get_ulong(d, "media.subsys", 0);
   uint32_t dir = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_NA);
   uint32_t vfo = dict_get_ulong(d, "media.vfo", RR_BINFRAME_VFO_NA);
   uint32_t rig = dict_get_ulong(d, "media.rig", RR_BINFRAME_RIG_NA);

   if (uuid && uuid[0] != '\0') {
      struct rr_client_media_chan *kp = media_known_add(uuid);

      if (kp) {
         const char *descr = dict_get(d, "media.descr", NULL);
         const char *name = dict_get(d, "media.name", NULL);

         snprintf(kp->profiles, sizeof(kp->profiles), "%s", dict_get(d, "media.profiles", ""));
         kp->subsystem = subsys;
         kp->direction = dir;
         kp->vfo = vfo;
         kp->rig = rig;
         snprintf(kp->rig_uuid, sizeof(kp->rig_uuid), "%s", dict_get(d, "media.rig-uuid", ""));
         snprintf(kp->vfo_uuid, sizeof(kp->vfo_uuid), "%s", dict_get(d, "media.vfo-uuid", ""));
         const char *room = dict_get(d, "media.room", "");
         snprintf(kp->room, sizeof(kp->room), "%s", room);
         snprintf(kp->control_room, sizeof(kp->control_room), "%s", dict_get(d, "media.control-room", room));
         kp->joined = dict_get_bool(d, "media.joined", false);

         if (kp->joined && kp->direction == RR_BINFRAME_DIR_RX && (kp->vfo < 32 || kp->vfo == RR_BINFRAME_VFO_NA) &&
            ws_room_same_rig(media_room, kp->control_room) && rrclient_room_is_joined(media_room) &&
            (kp->vfo == RR_BINFRAME_VFO_NA ? rrclient_room_vfo_mask(media_room) != 0 : (rrclient_room_vfo_mask(media_room) & (UINT32_C(1) << kp->vfo)) != 0)) {
            snprintf(kp->room, sizeof(kp->room), "%s", media_room);
         }

         if (kp->room[0] && !kp->joined) {
            kp->subscribed = false;
            kp->stream_valid = false;
         }

         if (kp->room[0] && kp->joined && !media_room[0]) {
            snprintf(media_room, sizeof(media_room), "%s", kp->room);
         }

         if (name && name[0] != '\0') {
            snprintf(kp->name, sizeof(kp->name), "%s", name);
         }
         const char *codec = dict_get(d, "media.codec", NULL);

         if (codec && strlen(codec) == 4) {
            snprintf(kp->codec, sizeof(kp->codec), "%s", codec);
         }

         if ((!kp->room[0] || kp->joined) && kp->disabled && kp->pending_codec[0] &&
            strcmp(kp->pending_codec, kp->codec) == 0 && cptr && media_send_subscribe(cptr, kp->uuid)) {
            kp->pending_codec[0] = '\0';
            kp->disabled = false;
            kp->subscribed = true;
         }

         if (descr && descr[0] != '\0') {
            snprintf(kp->descr, sizeof(kp->descr), "%s", descr);
         }
         media_try_autosubscribe(cptr, kp);
         gps_prefer_packed(cptr);
         rrclient_media_sync_audio();

      }
   }
}

// Called by events.c with the parsed subscribed/unsubscribed dict
void rrclient_media_subscribed(dict *d, rrconn_t *cptr, bool unsub) {
   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_client_media_chan *kp = (uuid ? media_known_find(uuid) : NULL);

   if (kp) {
      if (!unsub && (kp->disabled || (kp->room[0] && !kp->joined))) {
         if (ws_conn) {
            media_send_unsubscribe(ws_conn, kp->uuid);
         }

         return;
      }
      kp->subscribed = !unsub;

      if (unsub) {
         kp->stream = 0;
         kp->stream_valid = false;
      }
      const char *codec = dict_get(d, "media.codec", NULL);

      if (!unsub && dict_get_type(d, "media.stream") != VAL_END) {
         kp->stream = (uint8_t) dict_get_ulong(d, "media.stream", 0);
         kp->stream_valid = true;
      }

      if (codec && strlen(codec) == 4) {
         snprintf(kp->codec, sizeof(kp->codec), "%s", codec);
      }
      rrclient_media_sync_audio();

      char description[512];
      rrclient_media_description(kp->uuid, description, sizeof(description));
      Log(LOG_INFO, "ws.media", "Media subscription %s: %s", unsub ? "removed" : "confirmed", description);
   }
}

// Called by events.c with the parsed chan-remove dict
void rrclient_media_chan_removed(dict *d) {
   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_client_media_chan *kp = (uuid ? media_known_find(uuid) : NULL);

   if (kp) {
      char description[512];
      rrclient_media_description(kp->uuid, description, sizeof(description));
      Log(LOG_INFO, "ws.media", "Media channel removed: %s", description);
      memset(kp, 0, sizeof(*kp));
      rrclient_media_sync_audio();
   }
}

// Event: connection state changes. On (re)connect, reset our local channel
// table; the server pushes a fresh media.available batch after auth.
static void rrclient_handle_media_conn(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!event) {
      return;
   }

   if (strcasecmp(event, "disconnected") == 0) {
      media_room[0] = '\0';
      memset(known_chans, 0, sizeof(known_chans));
      memset(gps_position_cache, 0, sizeof(gps_position_cache));
      media_ready = false;
      memset(direction_disabled, 0, sizeof(direction_disabled));
      rrclient_media_sync_audio();
      media_my_privs[0] = '\0';
   } else if (strcasecmp(event, "authorized") == 0 && data) {
      dict *ad = json2dict(data);

      if (ad) {
         const char *privs = dict_get(ad, "auth.privs", NULL);

         if (privs) {
            snprintf(media_my_privs, sizeof(media_my_privs), "%s", privs);
         }
         dict_free(ad);
      }
      media_ready = true;
   } else if (strcasecmp(event, "connected") == 0) {
      media_ready = true;
   }
}

/* PARITY: rustyrig-www/js/webui.chat.js:parse_userinfo_reply */
static void rrclient_media_account_update(const char *event, const char *data, rrconn_t *cptr, void *user) {
   dict *update = data ? json2dict(data) : NULL;
   const char *name = update ? dict_get(update, "talk.user", NULL) : NULL;
   const char *privileges = update ? dict_get(update, "talk.privs", NULL) : NULL;

   if (name && login_user && !strcasecmp(name, login_user) && privileges) {
      snprintf(media_my_privs, sizeof(media_my_privs), "%s", privileges);
   }

   if (update) {
      dict_free(update);
   }
}

static void rrclient_handle_media_codecs(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)user;

   const char *codec = media_get_preferred_codec();
   rrconn_t *conn = cptr;

   if (!conn || !codec || strlen(codec) != 4) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0]) {
         media_try_autosubscribe(conn, &known_chans[i]);
      }
   }

   rrclient_media_sync_audio();
}

// Keep playback and microphone selection tied to the VFO shown by the UI.
// Retained audio subscriptions must not change the microphone destination.
static void rrclient_handle_media_vfo(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)user;

   if (!cptr || !media_ready) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];
      bool gps_metadata = kp->subsystem == RR_BINFRAME_SUBSYS_MODEM && (!strcmp(kp->codec, RR_GPS_FRAME_CODEC) || !strcmp(kp->codec, RR_NMEA_FRAME_CODEC));
      /* Retain subscribed site position feeds across room/tab changes;
       * automatic acquisition still requires a joined rig room. */
      bool active = (gps_metadata && kp->subscribed) || gps_wanted(kp) ||
         (kp->subsystem == RR_BINFRAME_SUBSYS_AUDIO &&
            (kp->vfo == RR_BINFRAME_VFO_NA || kp->vfo == (uint8_t) (vfo_state_get_active() - 'A')) &&
            (!kp->room[0] || (kp->joined && !strcasecmp(kp->room, media_room))));

      // PARITY: rustyrig-www/js/webui.media.js mediaSyncActiveVfo.
      // Tab/VFO selection controls TX routing; room output choices control RX playback.
      bool retain_audio = kp->subsystem == RR_BINFRAME_SUBSYS_AUDIO && (!kp->room[0] || kp->joined);

      if (kp->automatic && kp->subscribed && !active && !retain_audio) {
         media_send_unsubscribe(cptr, kp->uuid);
         kp->subscribed = false;
         kp->stream_valid = false;
      }
      media_try_autosubscribe(cptr, kp);
   }

   rrclient_media_sync_audio();
}

void rrclient_media_room_joined(const char *room) {
   if (!room || !*room) {
      return;
   }
   snprintf(media_room, sizeof(media_room), "%s", room);
   uint32_t mask = rrclient_room_vfo_mask(room);
   char active = vfo_state_get_active();

   if (mask && (active < 'A' || active > 'Z' || !(mask & (UINT32_C(1) << (active - 'A'))))) {
      for (unsigned int i = 0 ; i < 26 ; i++) {
         if (mask & (UINT32_C(1) << i)) {
            char id[2] = {
               (char)('A' + i), 0
            };
            vfo_state_set_active(id);
            event_emit("client.vfo.changed", NULL, id);
            break;
         }
      }
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (channel->direction == RR_BINFRAME_DIR_RX && (channel->vfo < 32 || channel->vfo == RR_BINFRAME_VFO_NA) &&
         ws_room_same_rig(room, channel->control_room) && rrclient_room_is_joined(room)) {
         snprintf(channel->room, sizeof(channel->room), "%s", room);
         channel->joined = channel->vfo == RR_BINFRAME_VFO_NA ? mask != 0 : (mask & (UINT32_C(1) << channel->vfo)) != 0;
      }
   }

   rrclient_handle_media_vfo(NULL, NULL, NULL, NULL);
}

void rrclient_media_room_selected(const char *room) {
   if (!room || !strcasecmp(room, media_room)) {
      return;
   }

   if (rrclient_room_is_joined(room) && rrclient_room_vfo_mask(room)) {
      rrclient_media_room_joined(room);

      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].joined && !strcasecmp(known_chans[i].room, room)) {
         rrclient_media_room_joined(room);

         return;
      }
   }
}

/* The server has destroyed an entire room (including its media channels).
 * Do not issue media.unsubscribe while processing the removal: the UUIDs
 * are already gone on the server, so that request produces a spurious error.
 * Normal voluntary PART still uses rrclient_media_room_parted(). */
void rrclient_media_room_removed(const char *room) {
   if (!room || !*room) return;
   bool changed = false;
   for (int i = 0; i < RR_MEDIA_MAX_CHANS; ++i) {
      struct rr_client_media_chan *channel = &known_chans[i];
      if (channel->uuid[0] && !strcasecmp(channel->room, room)) {
         memset(channel, 0, sizeof(*channel));
         changed = true;
      }
   }
   if (!strcasecmp(media_room, room)) {
      media_room[0] = '\0';
      changed = true;
   }
   /* Keep routing the remaining channels without re-subscribing a deleted
    * one. The regular VFO handler also performs one audio/UI sync. */
   if (ws_conn && media_ready)
      rrclient_handle_media_vfo(NULL, NULL, NULL, NULL);
   else if (changed)
      rrclient_media_sync_audio();
}

void rrclient_media_room_parted(const char *room) {
   if (!room) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (!strcasecmp(known_chans[i].room, room)) {
         known_chans[i].joined = false;
      }
   }

   if (!strcasecmp(media_room, room)) {
      media_room[0] = '\0';
   }
   rrclient_handle_media_vfo(NULL, NULL, NULL, NULL);
}

/* PARITY: rustyrig-www/js/webui.audio.framing.js binframe_gps_position/binframe_gps_nmea.
 */
static bool gps_sentence(int32_t lat, int32_t lon, uint8_t flags, char *sentence, size_t capacity) {
   return rr_gps_nmea_rmc(lat, lon, flags, time(NULL), sentence, capacity) != 0;
}
static void gps_frame(const char *event, const void *data, size_t len, rrconn_t *client, void *user) {
   struct rr_binframe frame;

   if (rr_binframe_parse(data, len, &frame) || len != RR_BINFRAME_HDR_LEN + frame.len ||
      frame.hdr.subsystem != RR_BINFRAME_SUBSYS_MODEM || frame.hdr.direction != RR_BINFRAME_DIR_RX ||
      frame.hdr.vfo != RR_BINFRAME_VFO_NA || !frame.hdr.stream) {
      return;
   }
   bool raw = !memcmp(frame.hdr.codec, RR_NMEA_FRAME_CODEC, 4);
   char sentence[512], coordinates[64] = "";

   if (raw) {
      if (!frame.len || frame.len >= sizeof(sentence) || memchr(frame.data, 0, frame.len)) {
         return;
      }
      memcpy(sentence, frame.data, frame.len);
      sentence[frame.len] = '\0';
   } else {
      if (memcmp(frame.hdr.codec, RR_GPS_FRAME_CODEC, 4) || frame.len != RR_GPS_POSITION_PAYLOAD_LEN) {
         return;
      }
      int32_t lat = (int32_t) ((uint32_t) frame.data[0] << 24 | (uint32_t) frame.data[1] << 16 | (uint32_t) frame.data[2] << 8 | frame.data[3]);
      int32_t lon = (int32_t) ((uint32_t) frame.data[4] << 24 | (uint32_t) frame.data[5] << 16 | (uint32_t) frame.data[6] << 8 | frame.data[7]);

      if (frame.data[8] & RR_GPS_POSITION_VALID) {
         long long latitude = lat, longitude = lon;
         snprintf(coordinates, sizeof(coordinates), "%s%lld.%07lld, %s%lld.%07lld", latitude < 0 ? "-" : "", llabs(latitude) / 10000000, llabs(latitude) %
            10000000, longitude < 0 ? "-" : "", llabs(longitude) / 10000000, llabs(longitude) % 10000000);
      } else {
         strlcpy(coordinates, "No fix", sizeof(coordinates));
      }

      if (!gps_sentence(lat, lon, frame.data[8], sentence, sizeof(sentence))) {
         return;
      }
   }

   if (!rr_nmea_valid(sentence)) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (!channel->subscribed || !channel->stream_valid || channel->stream != frame.hdr.stream ||
         channel->subsystem != RR_BINFRAME_SUBSYS_MODEM || strcmp(channel->codec, raw ? RR_NMEA_FRAME_CODEC : RR_GPS_FRAME_CODEC) ||
         channel->rig != frame.hdr.rig) {
         /* Position metadata can update a cached remote site even when its room is no longer the active tab. */
         continue;
      }

      if (raw) {
         gps_position_cache[channel - known_chans].known = true;
         snprintf(gps_position_cache[channel - known_chans].raw_nmea, sizeof(gps_position_cache[0].raw_nmea), "%s", sentence);
      }

      if (!raw) {
         size_t position_index = (size_t)(channel - known_chans);
         gps_position_cache[position_index].known = true;
         gps_position_cache[position_index].lat =
            (int32_t) ((uint32_t) frame.data[0] << 24 | (uint32_t) frame.data[1] << 16 | (uint32_t) frame.data[2] << 8 | frame.data[3]);
         gps_position_cache[position_index].lon =
            (int32_t) ((uint32_t) frame.data[4] << 24 | (uint32_t) frame.data[5] << 16 | (uint32_t) frame.data[6] << 8 | frame.data[7]);
         gps_position_cache[position_index].flags = frame.data[8];
         strlcpy(channel->gps_coordinates, coordinates, sizeof(channel->gps_coordinates));
      }
      const char *suffix = strstr(channel->name, raw ? ".nmea.rx" : ".gps.rx");

      if (!suffix) {
         return;
      }
      char source[64];
      size_t size = suffix - channel->name;

      if (size >= sizeof(source)) {
         return;
      }
      memcpy(source, channel->name, size);
      source[size] = '\0';
      dict *d = dict_new();

      if (!d) {
         return;
      }
      dict_add(d, "gps.channel-uuid", channel->uuid);
      dict_add(d, "gps.source", source);
      dict_add(d, "gps.nmea", sentence);
      dict_add_bool(d, "gps.raw", raw);
      dict_add_bool(d, "gps.selected", ws_room_same_rig(media_room, channel->control_room) ||
         (!strcmp(source, "station") && !rrclient_room_vfo_mask(media_room)));
      event_emit_dict("serial.gps.output", NULL, d);
      dict_free(d);

      return;
   }
}

static void media_server_selected(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)cptr;
   (void)user;
   rrclient_media_sync_audio();
}

static void media_description(const char *uuid, const char *codec, bool diagnostic, char *out, size_t capacity) {
   const struct rr_client_media_chan *channel = media_known_find(uuid);

   if (!channel) {
      snprintf(out, capacity, "%s (undiscovered)", uuid ? uuid : "unknown");

      return;
   }
   const dict *rig = rrclient_object_descriptor(channel->rig_uuid);
   const dict *station = rig ? rrclient_object_descriptor(dict_get((dict *) rig, "object.owner", "")) : NULL;
   char station_name[128] = "unknown", path[128];
   const char *room = channel->control_room[0] ? channel->control_room : channel->room;

   if (room[0] == '#') {
      snprintf(station_name, sizeof(station_name), "%s", room + 1);
      char *suffix = strstr(station_name, "-rig");

      if (suffix) {
         *suffix = '\0';
      }
   }
   const char *station_alias = station ? dict_get((dict *) station, "object.alias", station_name) : station_name;

   if (channel->name[0]) {
      snprintf(path, sizeof(path), "%s", channel->name);
   } else {
      char vfo[16] = "";

      if (channel->vfo < 26) {
         snprintf(vfo, sizeof(vfo), ".vfo-%c", 'a' + channel->vfo);
      }
      snprintf(path, sizeof(path), "rig%u%s.%s", channel->rig, vfo, channel->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx");
   }
   // PARITY: rustyrig-www/js/webui.media.js (confirmed transport caption).
   // Match diagnostic VFO spelling while retaining GPS/NMEA endpoint names.
   char *vfo = strstr(path, ".vfo_");

   if (vfo) {
      vfo[4] = '-';
   }
   snprintf(out, capacity, "%s.%s (codec=%s%s%s)", station_alias, path, codec && *codec ? codec : channel->codec[0] ? channel->codec : "unknown", diagnostic ?
      "; uuid=" : "", diagnostic ? channel->uuid : "");
}

void rrclient_media_description(const char *uuid, char *out, size_t capacity) {
   media_description(uuid, NULL, true, out, capacity);
}

static void media_transport_print(void *data) {
   dict *d = data;
   const char *room = dict_get(d, "media.room", "");
   const char *uuid = dict_get(d, "media.chan-uuid", "");
   char description[512];
   const char *status = dict_get(d, "media.status", "");
   bool intermediate = !strcmp(status, "requesting") || !strcmp(status, "connecting");
   media_description(uuid, dict_get(d, "media.codec", ""), intermediate, description, sizeof(description));
   const char *transport = dict_get(d, "media.transport", "");
   const char *endpoint = dict_get(d, "media.endpoint", "");

   if (intermediate) {
      Log(LOG_DEBUG, "ws.media", "Media %s: %s via %s%s%s", description, status, transport, *endpoint ? " at " : "", endpoint);

      return;
   }
   const char *fallback = dict_get(d, "media.fallback", "");

   if (*fallback) {
      ui_print(*room ? room : NULL, "%sMedia %s: %s %s%s%s; falling back to %s", get_chat_ts(now), description, transport, status, *endpoint ? " at " : "",
         endpoint, fallback);

      return;
   }
   /* PARITY: rustyrig-www/js/webui.media.js confirmed connection notices. */
   ui_print(*room ? room : NULL, "%sMedia %s: %s via %s%s%s", get_chat_ts(now), description, status, transport, *endpoint ? " at " : "", endpoint);
}
static void media_transport_notice(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   dict *d = data ? json2dict(data) : NULL;

   if (!d) {
      return;
   }
   rrclient_connection_dispatch(client, media_transport_print, d);
   dict_free(d);
}

void rrclient_media_register_events(void) {
   if (!gps_nmea_timer) {
      gps_nmea_timer = g_timeout_add_seconds(1, gps_nmea_tick, NULL);
   }
   event_on("client.media.transport", media_transport_notice, NULL);
   event_on("client.server.selected", media_server_selected, NULL);
   event_on("serial.gps.outputs.changed", gps_outputs_changed, NULL);
   event_on_binary(RR_GPS_FRAME_EVENT, gps_frame, NULL);
   event_on_binary(RR_NMEA_FRAME_EVENT, gps_frame, NULL);
   // media.* messages are dispatched directly from events.c (see
   // rrclient_handle_media) with the parsed dict; only connection state
   // needs the event bus here.
   event_on("connected", rrclient_handle_media_conn, NULL);
   event_on("media.codecs", rrclient_handle_media_codecs, NULL);
   event_on("client.vfo.changed", rrclient_handle_media_vfo, NULL);
   event_on("authorized", rrclient_handle_media_conn, NULL);
   event_on("userinfo", rrclient_media_account_update, NULL);
   event_on("disconnected", rrclient_handle_media_conn, NULL);
}

/* PARITY: rustyrig-www/js/webui.media.js (channel list / subscribe handling) */

// How many available channels are currently stored
int rrclient_media_chan_count(void) {
   int n = 0;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] != '\0') {
         n++;
      }
   }

   return n;
}

// Walk stored channels for completion providers etc: idx 0..count-1 over the
// non-empty slots; `listno` gets the 1-based number /media LIST shows.
const struct rr_client_media_chan *rrclient_media_chan_iter(int idx, int *listno) {
   int n = 0;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] == '\0') {
         continue;
      }

      if (n == idx) {
         if (listno) {
            *listno = n + 1;
         }

         return &known_chans[i];
      }
      n++;
   }

   if (listno) {
      *listno = 0;
   }

   return NULL;
}

// Get channel slot `idx` (0..count-1). Returns NULL when out of range.
const struct rr_client_media_chan *rrclient_media_chan_get(int idx) {
   int n = 0;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] == '\0') {
         continue;
      }

      if (n++ == idx) {
         return &known_chans[i];
      }
   }

   return NULL;
}

// PARITY: rustyrig-www/js/webui.media.js mediaChanLookup.
// Resolve UUID/index or a unique symbolic name; ambiguous names require UUID.
// Returns NULL when not found.
const struct rr_client_media_chan *rrclient_media_chan_lookup(const char *arg) {
   if (!arg || arg[0] == '\0') {
      return NULL;
   }
   struct rr_client_media_chan *kp = media_known_find(arg);

   if (kp) {
      return kp;
   }
   // Try a 1-based index into the stored list
   const char *number = arg[0] == '#' ? arg + 1 : arg;
   char *end;
   long index = strtol(number, &end, 10);

   if (end != number && !*end && index > 0 && index <= RR_MEDIA_MAX_CHANS) {
      return rrclient_media_chan_get((int)index - 1);
   }
   const struct rr_client_media_chan *match = NULL;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      const struct rr_client_media_chan *channel = &known_chans[i];

      if (!channel->uuid[0] || !channel->name[0] || strcasecmp(channel->name, arg)) {
         continue;
      }

      if (match) {
         return NULL;
      }
      match = channel;
   }

   return match;
}

// Subscribe (or unsubscribe) to a channel by uuid. Returns false on OK.
bool rrclient_media_subscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   bool sent = media_send_subscribe(cptr, uuid);

   if (!sent) {
      media_print("Failed to subscribe to media channel uuid %s", uuid);
   }
   struct rr_client_media_chan *kp = media_known_find(uuid);

   if (sent && kp) {
      kp->automatic = false;
      kp->disabled = false;
      kp->pending_codec[0] = '\0';
   }

   return !sent;
}

bool rrclient_media_video_info(char *uuid, size_t uuid_size, char *profiles, size_t profiles_size) {
   const struct rr_client_media_chan *video = NULL;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] && known_chans[i].subsystem == RR_BINFRAME_SUBSYS_VIDEO && known_chans[i].direction == RR_BINFRAME_DIR_RX) {
         if (video) {
            return false;
         }
         video = &known_chans[i];
      }
   }

   if (!video) {
      return false;
   }
   snprintf(uuid, uuid_size, "%s", video->uuid);
   snprintf(profiles, profiles_size, "%s", video->profiles);

   return true;
}

// Automatically subscribe when the server has exactly one receive video stream.
bool rrclient_media_subscribe_single_video(void) {
   struct rr_client_media_chan *video = NULL;

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *kp = &known_chans[i];

      if (!kp->uuid[0] || kp->subsystem != RR_BINFRAME_SUBSYS_VIDEO || kp->direction != RR_BINFRAME_DIR_RX) {
         continue;
      }

      if (video) {
         return false;
      }
      video = kp;
   }

   if (!video) {
      return false;
   }

   if (!video->subscribed && rrclient_media_subscribe(video->uuid)) {
      return false;
   }

   return true;
}

bool rrclient_media_unsubscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   bool sent = media_send_unsubscribe(cptr, uuid);

   if (!sent) {
      media_print("Failed to unsubscribe from media channel uuid %s", uuid);
   }
   struct rr_client_media_chan *kp = media_known_find(uuid);

   if (sent && kp) {
      kp->disabled = true;
      kp->subscribed = false;
      kp->pending_codec[0] = '\0';
      rrclient_media_sync_audio();
   }

   return !sent;
}

// Ask the server for a fresh media.available batch
void rrclient_media_refresh(rrconn_t *cptr) {

   /* Background refresh requests must not generate warnings after a
    * profile disconnect. A closed WebSocket is not a failed refresh. */
   if (cptr && cptr->authenticated && cptr->is_ws &&
       cptr->conn && !cptr->conn->is_closing && cptr->connected) {
      if (!media_send_list(cptr)) {
         Log(LOG_WARN, "ws.media",
            "Media list refresh failed: websocket send rejected (connection=%p, connected=%d); check protocol send-validation logs", (void *)cptr, cptr->connected);
         media_print("Unable to refresh media channels; connection is not writable");
      }
   } else {
      Log(LOG_DEBUG, "ws.media", "Unable to refresh media channel list: not connected");
   }
}

// /media [LIST | SUBSCRIBE <name|uuid|#number> | UNSUBSCRIBE <name|uuid|#number>] - the
// LIST form
// (or no args) shows known channels and our subscriptions.
// PARITY: rustyrig-www/js/webui.media.js (channel list / subscribe handling)
bool cmd_media(int argc, char **args) {
   const char *sub = (argc > 1 ? args[1] : NULL);

   if (sub && !strcasecmp(sub, "DIRECT")) {
      if (argc > 3 || (argc == 3 && strcasecmp(args[2], "on") && strcasecmp(args[2], "off") && strcasecmp(args[2], "status"))) {
         media_print("Usage: /media direct [on|off|status]");

         return true;
      }

      if (argc == 3 && strcasecmp(args[2], "status")) {
         if (!rrclient_direct_set_preference(ws_conn, !strcasecmp(args[2], "on"))) {
            media_print("Select a configured server before changing its direct-media preference");

            return true;
         }
      }
      const char *profile = rrclient_connection_name(ws_conn);
      media_print("Direct-media preference for %s: %s (explicit -direct/-proxy subscriptions retain their mode)", profile ? profile : "global default",
         rrclient_direct_preferred(profile) ? "on" : "off");

      return false;
   }

   if (!sub || sub[0] == '\0' || strcasecmp(sub, "LIST") == 0) {
      // List what we know about and our subscription state
      media_print("\00311Available media channels:\017");
      int n = 0, visible = 0;

      for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
         struct rr_client_media_chan *kp = &known_chans[i];

         if (kp->uuid[0] == '\0') {
            continue;
         }
         n++;

         if (!rrclient_resource_matches(ui_active_window_name(), kp->control_room[0] ? kp->control_room : kp->room)) {
            continue;
         }
         visible++;
         media_print(" #%d %s [%s %s, %s] room=%s %s%s — %s (uuid=%s)", n, kp->name[0] ? kp->name : kp->uuid, kp->subsystem == RR_BINFRAME_SUBSYS_AUDIO ?
            "audio" :
            kp->subsystem == RR_BINFRAME_SUBSYS_MODEM ? "GPS/serial" : "media", kp->direction == RR_BINFRAME_DIR_TX ? "TX" : "RX", kp->codec[0] ? kp->codec :
            "----", kp->room[0] ? kp->room : "any", kp->subscribed ? "subscribed" : "unsubscribed", !kp->room[0]
            || kp->joined ? "" : " (join room first)", kp->descr[0] ? kp->descr : "-", kp->uuid);
      }

      media_print("End of list (%d channels). Use names, #numbers, or UUIDs.", visible);
      Log(LOG_INFO, "ws.media", "/media LIST: %d stored channels", n);

      if (sub) {
         return false;          // explicit LIST: no refresh
      }
      rrclient_media_refresh(ws_conn);

      return false;
   }

   if (strcasecmp(sub, "SUBSCRIBE") == 0 || strcasecmp(sub, "SUB") == 0 || strcasecmp(sub, "UNSUBSCRIBE") == 0 || strcasecmp(sub, "UNSUB") == 0) {
      bool unsub = (strncasecmp(sub, "UN", 2) == 0);

      const char *target = NULL;
      rr_media_transport_t transport = RR_MEDIA_AUTO;
      bool explicit_transport = false;

      for (int i = 2 ; i < argc ; i++) {
         if (!strcasecmp(args[i], "-direct") || !strcasecmp(args[i], "-proxy")) {
            if (unsub || explicit_transport) {
               media_print("Use one transport option with /media subscribe");

               return true;
            }
            transport = !strcasecmp(args[i], "-direct") ? RR_MEDIA_DIRECT : RR_MEDIA_PROXY;
            explicit_transport = true;
         } else if (!target && args[i][0] != '-') {
            target = args[i];
         } else {
            media_print("Usage: /media %s [-direct|-proxy] <name|uuid|#number>", sub);

            return true;
         }
      }

      if (!target || !*target) {
         media_print("Usage: /media %s [-direct|-proxy] <name|uuid|#number>", sub);

         return true;
      }
      const struct rr_client_media_chan *kp = rrclient_media_chan_lookup(target);

      if (!kp) {
         media_print("Unknown or ambiguous channel '%s'; use /media list and choose a name, #number or UUID", target);

         return true;
      }

      if (explicit_transport && ((transport == RR_MEDIA_DIRECT && kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO) ||
         !rrclient_direct_set_mode(ws_conn, kp->uuid, transport))) {
         media_print("The selected direct media transport is unavailable in this client or channel");

         return true;
      }

      if (unsub) {
         if (!kp->subscribed) {
            media_print("Not subscribed to %s", kp->name[0] ? kp->name : kp->uuid);

            return false;
         }
         media_print("Unsubscribing from %s (%s)", kp->name[0] ? kp->name : kp->uuid, (kp->descr[0] != '\0' ? kp->descr : "-"));

         return rrclient_media_unsubscribe(kp->uuid);
      }

      if (kp->subscribed && !explicit_transport) {
         media_print("Already subscribed to %s (%s)", kp->name[0] ? kp->name : kp->uuid, (kp->descr[0] != '\0' ? kp->descr : "-"));

         return false;
      }
      media_print("Subscribing to %s (%s)", kp->name[0] ? kp->name : kp->uuid, (kp->descr[0] != '\0' ? kp->descr : "-"));

      return rrclient_media_subscribe(kp->uuid);
   }
   media_print("Usage: /media [DIRECT [on|off|status] | LIST | SUB|SUBSCRIBE [-direct|-proxy] <name|uuid|#number> | UNSUB|UNSUBSCRIBE <name|uuid|#number>]");

   return true;
}

// Shared GTK/TUI commands: list codecs and channel state, or select by UUID.
static bool cmd_audio_codec(int argc, char **args, bool is_tx) {
   const char *command = is_tx ? "txcodec" : "rxcodec";

   if (argc > 3 || (argc == 3 && strcasecmp(args[1], "list") == 0)) {
      media_print("Usage: /%s [LIST | <codec>|NONE [name|uuid|#number]]", command);

      return true;
   }

   if (argc < 2 || strcasecmp(args[1], "list") == 0) {
      const char *list = media_ready ? media_get_common_codecs() : NULL;
      media_print("%s codecs: NONE %s", is_tx ? "TX" : "RX", list ? list : "(not negotiated)");
      int number = 0, matches = 0;

      for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
         struct rr_client_media_chan *kp = &known_chans[i];

         if (!kp->uuid[0]) {
            continue;
         }
         number++;

         if (!rrclient_resource_matches(ui_active_window_name(), kp->control_room[0] ? kp->control_room : kp->room)) {
            continue;
         }

         if (kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
            kp->direction != (is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX) || (!kp->subscribed && !kp->disabled)) {
            continue;
         }
         media_print(" #%d %s [%s]: %s%s (%s)", number, (kp->name[0] ? kp->name : "-"), kp->uuid, kp->disabled ? "NONE" : kp->codec, kp->pending_codec[0] ?
            " (selection pending)" : "", kp->descr);
         matches++;
      }

      if (!matches) {
         media_print("No subscribed %s audio channels", is_tx ? "TX" : "RX");
      }

      return false;
   }

   if (!ws_conn || !media_ready) {
      media_print("Connect to a server before selecting codecs");

      return true;
   }

   if (strlen(args[1]) != 4) {
      media_print("Use /%s LIST to see supported codecs", command);

      return true;
   }
   const struct rr_client_media_chan *target = (argc == 3) ? rrclient_media_chan_lookup(args[2]) : media_codec_target_channel(is_tx);
   if (!target) {
      media_print("No selected %s media channel; use /media LIST", is_tx ? "TX" : "RX");
      return true;
   }
   bool failed = media_select_codec(ws_conn, is_tx, args[1], target->uuid);

   if (!failed) {
      char description[512];
      rrclient_media_description(target ? target->uuid : NULL, description, sizeof(description));
      const char *room = target && target->room[0] ? target->room : media_output_window();
      ui_print(room, "Requested %s codec %s for %s", is_tx ? "TX" : "RX", args[1], description);
   }

   return failed;
}

bool cmd_rxcodec(int argc, char **args) {
   return cmd_audio_codec(argc, args, false);
}

bool cmd_txcodec(int argc, char **args) {
   return cmd_audio_codec(argc, args, true);
}

const char *rrclient_media_active_room(void) {
   return media_room;
}

const char *rrclient_media_rig_coordinates(const char *room) {
   if (!room || !*room) {
      return NULL;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; ++i) {
      const struct rr_client_media_chan *channel = &known_chans[i];

      if (channel->gps_coordinates[0] && channel->control_room[0] && ws_room_same_rig(room, channel->control_room)) {
         return channel->gps_coordinates;
      }
   }

   return NULL;
}

const char *rrclient_media_vfo_uuid(const char *room, char vfo) {
   if (!room || vfo < 'A' || vfo > 'Z') {
      return NULL;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_client_media_chan *channel = &known_chans[i];

      if (channel->vfo == RR_BINFRAME_VFO_NA && channel->rig_uuid[0] && ws_room_same_rig(room, channel->control_room)) {
         char alias[2] = {
            vfo, 0
         };
         const dict *object = rrclient_object_find_alias("vfo", channel->rig_uuid, alias);

         if (object) {
            return dict_get((dict *) object, "object.uuid", NULL);
         }
      }

      if (channel->vfo == vfo - 'A' && channel->vfo_uuid[0] && ws_room_same_rig(room, channel->control_room)) {
         return channel->vfo_uuid;
      }
   }

   return NULL;
}

/* Save/load the component view when entering a server connection context. */
typedef struct {
   char media_my_privs[128];
   struct rr_client_media_chan known_chans[RR_MEDIA_MAX_CHANS];
   gps_position_cache_t gps_position_cache[RR_MEDIA_MAX_CHANS];
   char media_room[128], gps_scopes[1024];
   bool media_ready, direction_disabled[2];
} rrclient_media_context;
void rrclient_media_context_swap(void **saved) {
   if (!*saved) {
      *saved = calloc(1, sizeof(rrclient_media_context));

      if (!*saved) {
         abort();
      }
   }
   rrclient_media_context *state = *saved;
   {
      __typeof__(media_my_privs) temporary;
      memcpy(&temporary, &media_my_privs, sizeof(media_my_privs));
      memcpy(&media_my_privs, &state->media_my_privs, sizeof(media_my_privs));
      memcpy(&state->media_my_privs, &temporary, sizeof(media_my_privs));
   }
   {
      __typeof__(known_chans) temporary;
      memcpy(&temporary, &known_chans, sizeof(known_chans));
      memcpy(&known_chans, &state->known_chans, sizeof(known_chans));
      memcpy(&state->known_chans, &temporary, sizeof(known_chans));
   }
   {
      __typeof__(gps_position_cache) temporary;
      memcpy(&temporary, &gps_position_cache, sizeof(gps_position_cache));
      memcpy(&gps_position_cache, &state->gps_position_cache, sizeof(gps_position_cache));
      memcpy(&state->gps_position_cache, &temporary, sizeof(gps_position_cache));
   }
   {
      __typeof__(media_room) temporary;
      memcpy(&temporary, &media_room, sizeof(media_room));
      memcpy(&media_room, &state->media_room, sizeof(media_room));
      memcpy(&state->media_room, &temporary, sizeof(media_room));
   }
   {
      __typeof__(gps_scopes) temporary;
      memcpy(&temporary, &gps_scopes, sizeof(gps_scopes));
      memcpy(&gps_scopes, &state->gps_scopes, sizeof(gps_scopes));
      memcpy(&state->gps_scopes, &temporary, sizeof(gps_scopes));
   }
   {
      __typeof__(media_ready) temporary;
      memcpy(&temporary, &media_ready, sizeof(media_ready));
      memcpy(&media_ready, &state->media_ready, sizeof(media_ready));
      memcpy(&state->media_ready, &temporary, sizeof(media_ready));
   }
   {
      __typeof__(direction_disabled) temporary;
      memcpy(&temporary, &direction_disabled, sizeof(direction_disabled));
      memcpy(&direction_disabled, &state->direction_disabled, sizeof(direction_disabled));
      memcpy(&state->direction_disabled, &temporary, sizeof(direction_disabled));
   }
}

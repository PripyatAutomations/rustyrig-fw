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
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrclient/vfo.h>
#include <rrclient/audio.h>
#include <rrclient/media.h>

extern rrconn_t *ws_conn;
extern bool ui_print(const char *window, const char *fmt, ...);

// Privileges the server granted us at auth (e.g. "admin,edit,view,...")
static char media_my_privs[128] = { 0 };

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

      if (len >= sizeof(tmp) ) {
         len = sizeof(tmp) - 1;
      }
      memcpy(tmp, p, len);
      tmp[len] = '\0';
      if (match_priv(media_my_privs, tmp) ) {
         return true;
      }
      p = sep ? sep + 1 : NULL;
   }
   return false;
}

#define RR_MEDIA_MAX_CHANS 64

struct rr_media_known {
   char uuid[64];
   uint8_t subsystem;
   uint8_t direction;
   uint8_t vfo;
   uint8_t rig;
   char codec[5];                  // active (negotiated) codec magic
   char descr[128];
   bool subscribed;
   bool disabled;                 // explicit NONE, retained for re-enabling
   char pending_codec[5];         // wait for confirmation before resubscribing
};

static struct rr_media_known known_chans[RR_MEDIA_MAX_CHANS];
static bool media_ready = false;         // have we got the first available batch?
static bool direction_disabled[2];
const struct rr_media_known *rrclient_media_chan_lookup(const char *arg);

static struct rr_media_known *media_known_find(const char *uuid) {
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0] != '\0' && strcasecmp(known_chans[i].uuid, uuid) == 0) {
         return &known_chans[i];
      }
   }
   return NULL;
}

static struct rr_media_known *media_known_add(const char *uuid) {
   struct rr_media_known *kp = media_known_find(uuid);

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

// One local pipeline per direction; prefer a subscription on the active VFO.
const char *rrclient_media_current_codec(bool is_tx) {
   uint8_t direction = is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX;
   char vfo = vfo_state_get_active();
   const char *fallback = NULL;
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_media_known *kp = &known_chans[i];
      if (!kp->uuid[0] || !kp->subscribed || kp->disabled ||
          kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO || kp->direction != direction ||
          !kp->codec[0]) {
         continue;
      }
      if (kp->vfo == vfo - 'A' || kp->vfo == RR_BINFRAME_VFO_NA) {
         return kp->codec;
      }
      if (!fallback) {
         fallback = kp->codec;
      }
   }
   return fallback;
}

static void media_sync_audio(void) {
   for (int tx = 0 ; tx < 2 ; tx++) {
      const char *codec = rrclient_media_current_codec(tx);
      if (codec) {
         audio_switch_codec(codec, tx);
      } else {
         audio_stop_codec(tx);
      }
   }
   event_emit("client.media.changed", ws_conn, "");
}

static bool media_codec_supported(const char *codec) {
   const char *list = media_get_common_codecs();
   if (!list || !codec || strlen(codec) != 4) {
      return false;
   }
   while (*list) {
      while (*list == ' ') {
         list++;
      }
      const char *end = strchr(list, ' ');
      size_t len = end ? (size_t)(end - list) : strlen(list);
      if (len == 4 && strncasecmp(codec, list, 4) == 0) {
         return true;
      }
      if (!end) {
         break;
      }
      list = end + 1;
   }
   return false;
}

// NONE is local subscription intent, never an encoded format on the wire.
// The default applies to all subscribed (or explicitly disabled) audio channels.
static bool media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec,
   const char *target) {
   if (!cptr || !media_ready || !codec || strlen(codec) != 4) {
      return true;
   }
   bool none = strcasecmp(codec, "none") == 0;
   if (!none && !media_codec_supported(codec)) {
      ui_print(NULL, "Codec %s is not in the negotiated codec list", codec);
      return true;
   }
   const struct rr_media_known *selected = target ? rrclient_media_chan_lookup(target) : NULL;
   if (target && !selected) {
      ui_print(NULL, "No such media channel: %s", target);
      return true;
   }
   char normalized[5] = { 0 };
   for (int i = 0 ; i < 4 ; i++) {
      normalized[i] = (char)tolower((unsigned char)codec[i]);
   }
   uint8_t direction = is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX;
   bool sent = false, failed = false;
   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      struct rr_media_known *kp = &known_chans[i];
      if (!kp->uuid[0] || kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
          kp->direction != direction || (selected && kp != selected) ||
          (!kp->subscribed && !kp->disabled)) {
         continue;
      }
      if (none) {
         if (kp->subscribed && media_send_unsubscribe(cptr, kp->uuid)) {
            failed = true;
            continue;
         }
         kp->disabled = true;
         kp->subscribed = false;
         kp->pending_codec[0] = '\0';
      } else {
         if (media_send_codec_select(cptr, normalized, kp->uuid)) {
            failed = true;
            continue;
         }
         if (kp->disabled) {
            memcpy(kp->pending_codec, normalized, sizeof(kp->pending_codec));
         }
      }
      sent = true;
   }
   if (!target && !failed && (none || sent)) {
      direction_disabled[is_tx] = none;
   }
   media_sync_audio();
   if (!sent && (target || !none)) {
      ui_print(NULL, "No subscribed %s audio channels match; use /media LIST", is_tx ? "TX" : "RX");
   }
   return failed || (!sent && (target || !none));
}

bool rrclient_media_select_codec(rrconn_t *cptr, bool is_tx, const char *codec) {
   return media_select_codec(cptr, is_tx, codec, NULL);
}

// Track pending subscriptions so we only subscribe once per channel
static void media_try_autosubscribe(rrconn_t *cptr, struct rr_media_known *kp) {
   if (!cptr || !kp || !media_ready || kp->subscribed || kp->disabled) {
      return;
   }
   // Audio channels for the active VFO auto-subscribe; video channels
   // (webcam etc, VFO NA) also auto-subscribe so the viewer just works.
   // Other subsystems (waterfall, modem, ...) stay opt-in by the user.
   if (kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO && kp->subsystem != RR_BINFRAME_SUBSYS_VIDEO) {
      return;
   }
   if (kp->subsystem == RR_BINFRAME_SUBSYS_AUDIO) {
      // Only auto-subscribe the channels for the VFO the UI is currently
      // showing; channels for other VFOs stay available for the user to
      // switch to (multi-VFO RX rigs expose them all).
      char cur_vfo = vfo_state_get_active();
      uint8_t active_id = (cur_vfo >= 'A' && cur_vfo <= 'Z') ? (uint8_t)(cur_vfo - 'A') : 0;

      if (kp->vfo != active_id && kp->vfo != RR_BINFRAME_VFO_NA) {
         return;
      }
      if (direction_disabled[kp->direction == RR_BINFRAME_DIR_TX]) {
         kp->disabled = true;
         return;
      }
   }
   if (kp->subsystem == RR_BINFRAME_SUBSYS_AUDIO && kp->codec[0] == '\0') {
      const char *codec = media_get_preferred_codec();

      if (codec && strlen(codec) == 4) {
         media_send_codec_select(cptr, codec, kp->uuid);
      }
      return;
   }

   if (!media_send_subscribe(cptr, kp->uuid)) {
      kp->subscribed = true;
   }
}

// Called by events.c with the already-parsed media.available dict (no JSON
// round-trip: dotted keys don't survive dict2json -> json2dict reliably).
// Note cptr may be NULL (the ws.msg.* event fires without one), so storage
// never depends on it; autosubscribe uses ws_conn.
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
      struct rr_media_known *kp = media_known_add(uuid);

      if (kp) {
         const char *descr = dict_get(d, "media.descr", NULL);

         kp->subsystem = subsys;
         kp->direction = dir;
         kp->vfo = vfo;
         kp->rig = rig;
         const char *codec = dict_get(d, "media.codec", NULL);

         if (codec && strlen(codec) == 4) {
            snprintf(kp->codec, sizeof(kp->codec), "%s", codec);
         }
         if (kp->disabled && kp->pending_codec[0] &&
             strcmp(kp->pending_codec, kp->codec) == 0 && ws_conn &&
             !media_send_subscribe(ws_conn, kp->uuid)) {
            kp->pending_codec[0] = '\0';
            kp->disabled = false;
            kp->subscribed = true;
         }
         if (descr && descr[0] != '\0') {
            snprintf(kp->descr, sizeof(kp->descr), "%s", descr);
         }
         media_try_autosubscribe(ws_conn, kp);
         media_sync_audio();
      }
   }
}

// Called by events.c with the parsed subscribed/unsubscribed dict
void rrclient_media_subscribed(dict *d, bool unsub) {
   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_media_known *kp = (uuid ? media_known_find(uuid) : NULL);

   if (kp) {
      if (!unsub && kp->disabled) {
         if (ws_conn) {
            media_send_unsubscribe(ws_conn, kp->uuid);
         }
         return;
      }
      kp->subscribed = !unsub;
      const char *codec = dict_get(d, "media.codec", NULL);

      if (codec && strlen(codec) == 4) {
         snprintf(kp->codec, sizeof(kp->codec), "%s", codec);
      }
      media_sync_audio();
      Log(LOG_INFO, "ws.media", "Media subscription %s: %s (codec %s)",
         (unsub ? "removed" : "confirmed"), kp->uuid, (kp->codec[0] ? kp->codec : "none") );
   }
}

// Called by events.c with the parsed chan-remove dict
void rrclient_media_chan_removed(dict *d) {
   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_media_known *kp = (uuid ? media_known_find(uuid) : NULL);

   if (kp) {
      Log(LOG_INFO, "ws.media", "Media channel removed: %s (%s)", kp->uuid,
         (kp->descr[0] != '\0' ? kp->descr : "-"));
      memset(kp, 0, sizeof(*kp) );
      media_sync_audio();
   }
}

// Event: connection state changes. On (re)connect, reset our local channel
// table; the server pushes a fresh media.available batch after auth.
static void rrclient_handle_media_conn(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!event) {
      return;
   }
   if (strcasecmp(event, "disconnected") == 0) {
     memset(known_chans, 0, sizeof(known_chans) );
     media_ready = false;
     memset(direction_disabled, 0, sizeof(direction_disabled));
     media_sync_audio();
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

static void rrclient_handle_media_codecs(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)user;

   const char *codec = media_get_preferred_codec();
   rrconn_t *conn = cptr ? cptr : ws_conn;

   if (!conn || !codec || strlen(codec) != 4) {
      return;
   }

   for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
      if (known_chans[i].uuid[0]) {
         media_try_autosubscribe(conn, &known_chans[i]);
      }
   }
   media_sync_audio();
}

void rrclient_media_register_events(void) {
   // media.* messages are dispatched directly from events.c (see
   // rrclient_handle_media) with the parsed dict; only connection state
   // needs the event bus here.
   event_on("connected", rrclient_handle_media_conn, NULL);
   event_on("media.codecs", rrclient_handle_media_codecs, NULL);
   event_on("authorized", rrclient_handle_media_conn, NULL);
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
         return (const struct rr_client_media_chan *)&known_chans[i];
      }
      n++;
   }
   if (listno) {
      *listno = 0;
   }
   return NULL;
}

// Get channel slot `idx` (0..count-1). Returns NULL when out of range.
const struct rr_media_known *rrclient_media_chan_get(int idx) {
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

// Find a stored channel by (case-insensitive) uuid or 1-based list index.
// Returns NULL when not found.
const struct rr_media_known *rrclient_media_chan_lookup(const char *arg) {
   if (!arg || arg[0] == '\0') {
      return NULL;
   }
   struct rr_media_known *kp = media_known_find(arg);

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
   return NULL;
}

// Subscribe (or unsubscribe) to a channel by uuid. Returns false on OK.
bool rrclient_media_subscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   bool failed = media_send_subscribe(cptr, uuid);
   struct rr_media_known *kp = media_known_find(uuid);
   if (!failed && kp) {
      kp->disabled = false;
      kp->pending_codec[0] = '\0';
   }
   return failed;
}

bool rrclient_media_unsubscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   bool failed = media_send_unsubscribe(cptr, uuid);
   struct rr_media_known *kp = media_known_find(uuid);
   if (!failed && kp) {
      kp->disabled = true;
      kp->subscribed = false;
      kp->pending_codec[0] = '\0';
      media_sync_audio();
   }
   return failed;
}

// Ask the server for a fresh media.available batch
void rrclient_media_refresh(void) {
   rrconn_t *cptr = ws_conn;

   if (cptr) {
      media_send_list(cptr);
   }
}

// /media [LIST | SUBSCRIBE <uuid|#> | UNSUBSCRIBE <uuid|#>] - the LIST form
// (or no args) shows known channels and our subscriptions.
// PARITY: rustyrig-www/js/webui.media.js (channel list / subscribe handling)
bool cmd_media(int argc, char **args) {
   const char *sub = (argc > 1 ? args[1] : NULL);

   if (!sub || sub[0] == '\0' || strcasecmp(sub, "LIST") == 0) {
     // List what we know about and our subscription state
     ui_print(NULL, "{bright-cyan}Available media channels:{reset}");
     int n = 0;

     for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
        struct rr_media_known *kp = &known_chans[i];

        if (kp->uuid[0] == '\0') {
           continue;
        }
        n++;
        ui_print(NULL, " %2d. %s%s %s  [%s]  {magenta}%s{reset}", n,
           (kp->subscribed ? "{green}*{reset} " : "  "),
           (kp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), kp->uuid,
           (kp->codec[0] != '\0' ? kp->codec : "----"),
           (kp->descr[0] != '\0' ? kp->descr : "-") );
     }
     ui_print(NULL, "{bright-cyan}End of list ({reset}%d{bright-cyan} channels, {reset}*{bright-cyan} = subscribed){reset}", n);
     Log(LOG_INFO, "ws.media", "/media LIST: %d stored channels", n);

      if (sub) {
         return false;   // explicit LIST: no refresh
      }
      rrclient_media_refresh();
      return false;
   }
   if (strcasecmp(sub, "SUBSCRIBE") == 0 || strcasecmp(sub, "SUB") == 0 ||
       strcasecmp(sub, "UNSUBSCRIBE") == 0 || strcasecmp(sub, "UNSUB") == 0) {
      bool unsub = (strncasecmp(sub, "UN", 2) == 0);

      if (argc < 3 || !args[2] || args[2][0] == '\0') {
         ui_print(NULL, "Usage: /media %s <uuid|#>", sub);
         return true;
      }
      const struct rr_media_known *kp = rrclient_media_chan_lookup(args[2]);

      if (!kp) {
         if (unsub) {
            ui_print(NULL, "No such channel |%s|", args[2]);
            return true;
         }
         // Not in our table - pass the arg through as a creation request; the
         // server generates a new channel for subscribe-without-uuid.
         ui_print(NULL, "No stored channel matches |%s|; asking server to create one", args[2]);

         return rrclient_media_subscribe(args[2]);
      }
      if (unsub) {
         if (!kp->subscribed) {
            ui_print(NULL, "Not subscribed to %s", kp->uuid);
            return false;
         }
         ui_print(NULL, "Unsubscribing from %s (%s)", kp->uuid,
            (kp->descr[0] != '\0' ? kp->descr : "-"));

         return rrclient_media_unsubscribe(kp->uuid);
      }
      if (kp->subscribed) {
         ui_print(NULL, "Already subscribed to %s (%s)", kp->uuid,
            (kp->descr[0] != '\0' ? kp->descr : "-"));
         return false;
      }
      ui_print(NULL, "Subscribing to %s (%s)", kp->uuid,
         (kp->descr[0] != '\0' ? kp->descr : "-"));

      return rrclient_media_subscribe(kp->uuid);
   }
   ui_print(NULL, "Usage: /media [LIST | SUB|SUBSCRIBE <uuid|#> | UNSUB|UNSUBSCRIBE <uuid|#>]");

   return true;
}

// Shared GTK/TUI commands: list codecs and channel state, or select by UUID.
static bool cmd_audio_codec(int argc, char **args, bool is_tx) {
   const char *command = is_tx ? "txcodec" : "rxcodec";
   if (argc > 3 || (argc == 3 && strcasecmp(args[1], "list") == 0)) {
      ui_print(NULL, "Usage: /%s [LIST | <codec>|NONE [uuid|#number]]", command);
      return true;
   }
   if (argc < 2 || strcasecmp(args[1], "list") == 0) {
      const char *list = media_ready ? media_get_common_codecs() : NULL;
      ui_print(NULL, "%s codecs: NONE %s", is_tx ? "TX" : "RX",
         list ? list : "(not negotiated)");
      int number = 0, matches = 0;
      for (int i = 0 ; i < RR_MEDIA_MAX_CHANS ; i++) {
         struct rr_media_known *kp = &known_chans[i];
         if (!kp->uuid[0]) {
            continue;
         }
         number++;
         if (kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
             kp->direction != (is_tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX) ||
             (!kp->subscribed && !kp->disabled)) {
            continue;
         }
         ui_print(NULL, " #%d %s: %s%s (%s)", number, kp->uuid,
            kp->disabled ? "NONE" : kp->codec,
            kp->pending_codec[0] ? " (selection pending)" : "", kp->descr);
         matches++;
      }
      if (!matches) {
         ui_print(NULL, "No subscribed %s audio channels", is_tx ? "TX" : "RX");
      }
      return false;
   }
   if (!ws_conn || !media_ready) {
      ui_print(NULL, "Connect to a server before selecting codecs");
      return true;
   }
   if (strlen(args[1]) != 4) {
      ui_print(NULL, "Use /%s LIST to see supported codecs", command);
      return true;
   }
   bool failed = media_select_codec(ws_conn, is_tx, args[1], argc == 3 ? args[2] : NULL);
   if (!failed) {
      ui_print(NULL, "Requested %s codec %s%s%s", is_tx ? "TX" : "RX", args[1],
         argc == 3 ? " for " : "", argc == 3 ? args[2] : "");
   }
   return failed;
}

bool cmd_rxcodec(int argc, char **args) {
   return cmd_audio_codec(argc, args, false);
}

bool cmd_txcodec(int argc, char **args) {
   return cmd_audio_codec(argc, args, true);
}

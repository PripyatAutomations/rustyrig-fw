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
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <rrclient/vfo.h>

#define RR_MEDIA_MAX_CHANS 64

struct rr_media_known {
   char uuid[64];
   uint8_t subsystem;
   uint8_t direction;
   uint8_t vfo;
   uint8_t rig;
   bool subscribed;
};

static struct rr_media_known known_chans[RR_MEDIA_MAX_CHANS];
static bool media_ready = false;         // have we got the first available batch?

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

// Track pending subscriptions so we only subscribe once per channel
static void media_try_autosubscribe(rrconn_t *cptr, struct rr_media_known *kp) {
   if (!cptr || !kp || !media_ready || kp->subscribed) {
      return;
   }
   // Only audio channels are auto-subscribed for now; other subsystems
   // (waterfall, modem, ...) are opt-in by the user.
   if (kp->subsystem != RR_BINFRAME_SUBSYS_AUDIO) {
      return;
   }
   // Only auto-subscribe the channels for the VFO the UI is currently
   // showing; channels for other VFOs stay available for the user to
   // switch to (multi-VFO RX rigs expose them all).
   char cur_vfo = vfo_state_get_active();
   uint8_t active_id = (cur_vfo >= 'A' && cur_vfo <= 'Z') ? (uint8_t)(cur_vfo - 'A') : 0;

   if (kp->vfo != active_id && kp->vfo != RR_BINFRAME_VFO_NA) {
      return;
   }
   media_send_subscribe(cptr, kp->uuid);
   kp->subscribed = true;
}

// Event: a media.available message from the server (re-emitted by
// events.c rrclient_handle_media)
static void rrclient_handle_media_available(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!data || !cptr) {
      return;
   }
   dict *d = json2dict(data);

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
         kp->subsystem = subsys;
         kp->direction = dir;
         kp->vfo = vfo;
         kp->rig = rig;
         media_try_autosubscribe(cptr, kp);
      }
   }
   dict_free(d);
}

// Event: our subscribe was confirmed
static void rrclient_handle_media_subscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   Log(LOG_INFO, "ws.media", "Media subscription confirmed");
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
   } else if (strcasecmp(event, "connected") == 0 || strcasecmp(event, "authorized") == 0) {
      media_ready = true;
   }
}

void rrclient_media_register_events(void) {
   event_on("media.available", rrclient_handle_media_available, NULL);
   event_on("media.subscribed", rrclient_handle_media_subscribed, NULL);
   event_on("connected", rrclient_handle_media_conn, NULL);
   event_on("authorized", rrclient_handle_media_conn, NULL);
   event_on("disconnected", rrclient_handle_media_conn, NULL);
}

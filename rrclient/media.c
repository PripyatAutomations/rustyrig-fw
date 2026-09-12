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
   char descr[128];
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
         const char *descr = dict_get(d, "media.descr", NULL);

         kp->subsystem = subsys;
         kp->direction = dir;
         kp->vfo = vfo;
         kp->rig = rig;
         if (descr && descr[0] != '\0') {
            snprintf(kp->descr, sizeof(kp->descr), "%s", descr);
         }
         media_try_autosubscribe(cptr, kp);
      }
   }
   dict_free(d);
}

// Event: the server removed a channel (re-emitted by events.c)
static void rrclient_handle_media_chan_removed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_media_known *kp = (uuid ? media_known_find(uuid) : NULL);

   if (kp) {
      Log(LOG_INFO, "ws.media", "Media channel removed: %s (%s)", kp->uuid,
         (kp->descr[0] != '\0' ? kp->descr : "-"));
      memset(kp, 0, sizeof(*kp) );
   }
   dict_free(d);
}

// Event: our subscribe was confirmed
static void rrclient_handle_media_subscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (data) {
      dict *d = json2dict(data);

      if (d) {
         const char *uuid = dict_get(d, "media.chan-uuid", NULL);
         struct rr_media_known *kp = (uuid ? media_known_find(uuid) : NULL);

         if (kp) {
            kp->subscribed = true;
         }
         dict_free(d);
      }
   }
   Log(LOG_INFO, "ws.media", "Media subscription confirmed");
}

// Event: our unsubscribe was confirmed
static void rrclient_handle_media_unsubscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (data) {
      dict *d = json2dict(data);

      if (d) {
         const char *uuid = dict_get(d, "media.chan-uuid", NULL);
         struct rr_media_known *kp = (uuid ? media_known_find(uuid) : NULL);

         if (kp) {
            kp->subscribed = false;
         }
         dict_free(d);
      }
   }
   Log(LOG_INFO, "ws.media", "Media subscription removed");
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

void rrclient_media_register_events(void) {
   event_on("media.available", rrclient_handle_media_available, NULL);
   event_on("media.subscribed", rrclient_handle_media_subscribed, NULL);
   event_on("media.unsubscribed", rrclient_handle_media_unsubscribed, NULL);
   event_on("media.chan-removed", rrclient_handle_media_chan_removed, NULL);
   event_on("connected", rrclient_handle_media_conn, NULL);
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
   if (arg[0] >= '1' && arg[0] <= '9') {
      return rrclient_media_chan_get(atoi(arg) - 1);
   }
   return NULL;
}

// Subscribe (or unsubscribe) to a channel by uuid. Returns false on OK.
bool rrclient_media_subscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   return media_send_subscribe(cptr, uuid);
}

bool rrclient_media_unsubscribe(const char *uuid) {
   rrconn_t *cptr = ws_conn;

   if (!cptr || !uuid || uuid[0] == '\0') {
      return true;
   }
   return media_send_unsubscribe(cptr, uuid);
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
         ui_print(NULL, " %2d. %s%s %s  {magenta}%s{reset}", ++n,
            (kp->subscribed ? "{green}*{reset} " : "  "),
            (kp->direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), kp->uuid,
            (kp->descr[0] != '\0' ? kp->descr : "-"));
      }
      ui_print(NULL, "{bright-cyan}End of list ({reset}%d{bright-cyan} channels, {reset}*{bright-cyan} = subscribed){reset}", n);

      if (sub) {
         return false;   // explicit LIST: no refresh
      }
      rrclient_media_refresh();
      return false;
   }
   if (strcasecmp(sub, "SUBSCRIBE") == 0 || strcasecmp(sub, "UNSUBSCRIBE") == 0) {
      bool unsub = (strcasecmp(sub, "UNSUBSCRIBE") == 0);

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
   ui_print(NULL, "Usage: /media [LIST | SUBSCRIBE <uuid|#> | UNSUBSCRIBE <uuid|#>]");

   return true;
}

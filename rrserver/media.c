//
// rrserver/media.c: media channel provisioning
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// The server owns the media channel registry. We create one RX and one TX
// audio channel per rig VFO the backend exposes (some devices like the
// Radioberry can RX multiple VFOs independently, so channels are per-VFO,
// never assumed to be a single shared stream). When a client logs in we
// push a `media.available` message per channel; the client subscribes to
// the channels it wants (typically its RX/TX pair) with media.subscribe.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <rrserver/backend.h>

extern time_t now;

// Create the TX and RX audio channel for a VFO (if not already made)
static void media_setup_vfo(rr_vfo_t vfo) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return;
   }
   const char *vname = vfo_name(vfo);
   char descr[96];

   snprintf(descr, sizeof(descr), "RX audio VFO %s", vname);
   media_chan_add(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_RX, (uint8_t)vfo, 0, NULL, descr);

   snprintf(descr, sizeof(descr), "TX audio VFO %s", vname);
   media_chan_add(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0, NULL, descr);
}

// Provision channels for every VFO the rig exposes (rig.vfos in config)
void rrserver_media_init(void) {
   int nvfos = cfg_get_int("rig.vfos", 2);

   if (nvfos < 1) {
      nvfos = 1;
   }
   if (nvfos > MAX_VFOS) {
      nvfos = MAX_VFOS;
   }
   // Prefer the backend's own view of which VFOs exist (e.g. a Radioberry
   // exposes 4 independent RX VFOs); fall back to the rig.vfos config when
   // the backend can't answer yet.
   int made = 0;

   for (int i = 0 ; i < nvfos ; i++) {
      if (rr_be_vfo_supported( (rr_vfo_t)i) ) {
         media_setup_vfo( (rr_vfo_t)i);
         made++;
      }
   }
   if (made == 0) {
      for (int i = 0 ; i < nvfos ; i++) {
         media_setup_vfo( (rr_vfo_t)i);
      }
      made = nvfos;
   }
   Log(LOG_INFO, "ws.media", "Provisioned media channels for %d VFO(s)", made);
}

// Push media.available for every channel to one client. Fired from the
// auth sequence via the send-media-channels event.
static void rrserver_handle_send_media_channels(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!cptr) {
      return;
   }
   media_send_available_all(cptr);
}

// Remove a media channel by uuid and tell every connected client it went
// away. Fired from the remove-media-channel event; data is the channel uuid.
static void rrserver_handle_remove_media_channel(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   const char *uuid = (data ? (const char *)data : "");
   struct rr_mediachan *cp = media_chan_find_uuid(uuid);

   if (!cp) {
      Log(LOG_WARN, "ws.media", "remove-media-channel: unknown uuid |%s|", uuid);
      return;
   }
   // Tell every client before the channel (and its uuid) goes away
   media_send_chan_removed_all(cp);
   media_chan_remove(uuid);
}

// A client selected a codec for one direction (fired by ws.mediachan.c from
// media.cmd: codec). Spawn (or ref up) the fwdsp pipeline for that codec.
// data is a dict: media.codec, media.dir, optional media.chan-uuid.
static void rrserver_handle_codec_select(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   // event_emit_dict() JSON-encodes the payload, so parse it back here
   dict *d = json2dict(data);

   if (!d) {
      Log(LOG_WARN, "ws.media", "codec-select with unparseable payload");
      return;
   }
   const char *codec = dict_get(d, "media.codec", NULL);
   uint32_t dir = dict_get_ulong(d, "media.dir", RR_BINFRAME_DIR_RX);

   if (!codec || strlen(codec) != 4) {
      Log(LOG_WARN, "ws.media", "codec-select without a 4-char codec magic");
      dict_free(d);
      return;
   }
   bool is_tx = (dir == RR_BINFRAME_DIR_TX);
   bool fwdsp_tx = !is_tx;
   const char *channel_uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_mediachan *channel = channel_uuid ? media_chan_find_uuid(channel_uuid) :
      media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, dir, 0, 0);
   if (!channel_uuid && channel) {
      channel_uuid = channel->uuid;
   }
   int chan_id = fwdsp_codec_start(codec, fwdsp_tx, channel_uuid);

   if (chan_id < 0) {
      Log(LOG_CRIT, "ws.media", "Failed to start fwdsp pipeline for %s.%s", codec, (fwdsp_tx ? "tx" : "rx") );
      dict_free(d);
      return;
   }
   Log(LOG_INFO, "ws.media", "Started fwdsp pipeline %s.%s (chan %d) for %s", codec,
      (fwdsp_tx ? "tx" : "rx"), chan_id, (cptr ? cptr->chatname : "?"));
   dict_free(d);
}

void rrserver_media_register_events(void) {
   event_on("send-media-channels", rrserver_handle_send_media_channels, NULL);
   event_on("remove-media-channel", rrserver_handle_remove_media_channel, NULL);
   event_on("media.codec-select", rrserver_handle_codec_select, NULL);
}

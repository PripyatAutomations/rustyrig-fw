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
#include <libfwdspmgr/fwdsp-ctl.h>
#include <rrserver/backend.h>
#include <rrserver/media.h>

extern time_t now;

// Recording direction describes the radio, not the encoder/decoder process.
static void media_record_channel(struct rr_mediachan *channel, rrconn_t *talker, bool start) {
   bool tx = channel->direction == RR_BINFRAME_DIR_TX;
   if (!channel->codec[0] ||
       !fwdsp_find_channel_instance(channel->codec, !tx, channel->uuid)) {
      return;
   }
   if (start && !cfg_get_bool(tx ? "fwdsp.recording.tx" : "fwdsp.recording.rx", false)) {
      return;
   }
   if (start && !tx) {
      char always_key[64];
      snprintf(always_key, sizeof(always_key), "record.always.vfo_%c", 'a' + channel->vfo);
      if (!cfg_get_bool(always_key, false) && http_count_clients() == 0) {
         return;
      }
   }
   if (start && tx && (!talker || !talker->chatname[0])) {
      return;
   }
   bool failed = start ? fwdsp_cmd_start_record_named(channel->codec, !tx,
      channel->uuid, tx ? talker->chatname : "radio", tx) :
      fwdsp_cmd_stop_record_channel(channel->codec, !tx, channel->uuid);
   if (failed) {
      Log(LOG_WARN, "record", "Unable to %s recording for channel %s",
         start ? "start" : "stop", channel->uuid);
   }
}

// Keep RX recording tied to actual client demand. Codec processes can linger
// after a client disconnects, so the periodic server tick also stops an RX
// recorder that no longer has a listener and starts one when a client returns.
void rrserver_media_recording_tick(void) {
   bool clients = http_count_clients() > 0;

   for (int i = 0; i < MAX_MEDIA_CHANNELS; i++) {
      struct rr_mediachan *channel = &media_channels[i];
      if (!channel->uuid[0] || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
          channel->direction != RR_BINFRAME_DIR_RX || !channel->codec[0] ||
          !fwdsp_find_channel_instance(channel->codec, true, channel->uuid)) {
         continue;
      }

      char always_key[64];
      snprintf(always_key, sizeof(always_key), "record.always.vfo_%c", 'a' + channel->vfo);
      bool always = cfg_get_bool(always_key, false);
      bool recording = cfg_get_bool("fwdsp.recording.rx", false);
      if (!recording || (!always && !clients)) {
         fwdsp_cmd_stop_record_channel(channel->codec, true, channel->uuid);
      } else {
         media_record_channel(channel, NULL, true);
      }
   }
}

void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return;
   }
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0);
   if (channel) {
      media_record_channel(channel, talker, ptt);
   }
}

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
   const char *old_codec = dict_get(d, "media.old-codec", NULL);
   const char *channel_uuid = dict_get(d, "media.chan-uuid", NULL);
   struct rr_mediachan *channel = channel_uuid ? media_chan_find_uuid(channel_uuid) : NULL;

   if (!codec || strlen(codec) != 4 || !channel) {
      Log(LOG_WARN, "ws.media", "codec-select without a valid codec/channel");
      dict_free(d);
      return;
   }

   // Media direction is from the client's point of view. Server RX-channel
   // delivery therefore needs an encoder (fwdsp tx mode), while client TX
   // media needs a decoder (fwdsp rx mode).
   bool fwdsp_tx = (channel->direction == RR_BINFRAME_DIR_RX);
   int chan_id = fwdsp_codec_switch(old_codec, codec, fwdsp_tx, channel->uuid);

   if (chan_id < 0) {
      Log(LOG_CRIT, "ws.media", "Failed to switch fwdsp pipeline to %s.%s for %s",
         codec, (fwdsp_tx ? "tx" : "rx"), channel->uuid);
      dict_free(d);
      return;
   }

   rrconn_t *talker = whos_talking();
   if (channel->direction == RR_BINFRAME_DIR_RX ||
       (talker && talker->ptt_vfo == 'A' + channel->vfo)) {
      media_record_channel(channel, talker, true);
   }

   Log(LOG_INFO, "ws.media", "Active fwdsp pipeline %s.%s (chan %d) for %s channel %s", codec,
      (fwdsp_tx ? "tx" : "rx"), chan_id, (cptr ? cptr->chatname : "?"), channel->uuid);
   dict_free(d);
}

// Late subscribers need container/codec headers before the next media packet.
static void rrserver_media_subscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   dict *d = data ? json2dict(data) : NULL;
   const char *uuid = d ? dict_get(d, "media.chan-uuid", NULL) : NULL;
   if (uuid && cptr) fwdsp_send_stream_headers(uuid, cptr);
   if (d) dict_free(d);
}

void rrserver_media_register_events(void) {
   event_on("media.subscribed", rrserver_media_subscribed, NULL);
   event_on("send-media-channels", rrserver_handle_send_media_channels, NULL);
   event_on("remove-media-channel", rrserver_handle_remove_media_channel, NULL);
   event_on("media.codec-select", rrserver_handle_codec_select, NULL);
}

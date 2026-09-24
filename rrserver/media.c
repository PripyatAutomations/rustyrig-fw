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
#include <rrserver/ptt.h>

extern time_t now;

struct media_record_log_state {
   char uuid[64];
   bool active;
};
static struct media_record_log_state media_record_logs[MAX_MEDIA_CHANNELS];

static time_t rig_rx_write_warned[MAX_MEDIA_CHANNELS];
static time_t rig_tx_write_warned;

static void rrserver_rig_rx_pcm(const char *name, const void *samples, size_t len,
   void *user_data) {
   (void)name;
   (void)user_data;
   for (int i = 0; i < MAX_MEDIA_CHANNELS; i++) {
      struct rr_mediachan *channel = &media_channels[i];
      if (!channel->uuid[0] || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
          channel->direction != RR_BINFRAME_DIR_RX || channel->rig != 0 ||
          !channel->codec[0]) {
         continue;
      }
      char always_key[64];
      snprintf(always_key, sizeof(always_key), "record.always.vfo_%c", 'a' + channel->vfo);
      if (!ws_media_channel_has_subscribers(channel) && !cfg_get_bool(always_key, false)) {
         continue;
      }
      if (!fwdsp_write_channel_samples(channel->codec, true, channel->uuid, samples, len) &&
          (rig_rx_write_warned[i] == 0 || now < rig_rx_write_warned[i] ||
           now - rig_rx_write_warned[i] >= 5)) {
         rig_rx_write_warned[i] = now;
         Log(LOG_WARN, "pcm.hub", "Unable to feed rig RX PCM to %s.tx for channel %s",
            channel->codec, channel->uuid);
      }
   }
}

static void rrserver_talker_pcm(const char *channel_uuid, const void *samples, size_t len,
   void *user_data) {
   (void)user_data;
   if (!channel_uuid || !*channel_uuid || !fwdsp_processor_write("sink.rig0", samples, len)) {
      if (rig_tx_write_warned == 0 || now < rig_tx_write_warned ||
          now - rig_tx_write_warned >= 5) {
         rig_tx_write_warned = now;
         Log(LOG_WARN, "pcm.hub", "Unable to feed decoded talker PCM to sink.rig0 (%s)",
            (channel_uuid && *channel_uuid) ? channel_uuid : "unknown channel");
      }
   }
}

bool rrserver_media_audio_init(void) {
   const char *configured_rx_source = cfg_get_exp("fwdsp:rig0.rx-source");
   const char *rx_source = configured_rx_source && *configured_rx_source ?
      configured_rx_source : "src.rig0";
   bool source_ok = fwdsp_audio_capture_start(rx_source, NULL, rrserver_rig_rx_pcm, NULL);
   bool sink_ok = fwdsp_audio_playback_start("sink.rig0", NULL);
   if (!source_ok || !sink_ok) {
      Log(LOG_CRIT, "pcm.hub", "Unable to start rig PCM endpoints (%s=%s, sink.rig0=%s)",
         rx_source, source_ok ? "ready" : "failed", sink_ok ? "ready" : "failed");
      free((char *)configured_rx_source);
      return false;
   }
   Log(LOG_INFO, "pcm.hub", "Rig PCM endpoints ready: %s -> per-channel encoders; TX decoders -> sink.rig0",
      rx_source);
   free((char *)configured_rx_source);
   return true;
}

static bool media_record_log_transition(const char *uuid, bool active) {
   if (!uuid || !*uuid) return true;
   struct media_record_log_state *slot = NULL;
   for (int i = 0; i < MAX_MEDIA_CHANNELS; i++) {
      if (!media_record_logs[i].uuid[0] && !slot) slot = &media_record_logs[i];
      if (media_record_logs[i].uuid[0] && !strcmp(media_record_logs[i].uuid, uuid)) {
         slot = &media_record_logs[i];
         break;
      }
   }
   if (!slot) return true;
   if (!slot->uuid[0]) snprintf(slot->uuid, sizeof(slot->uuid), "%s", uuid);
   if (slot->active == active) return false;
   slot->active = active;
   return true;
}

static bool media_recording_enabled(bool tx) {
   // Current server configs may keep recording policy under [fwdsp]. The
   // fwdsp defaults are always loaded, though, so merely checking whether
   // that key exists would hide an explicitly enabled legacy record.tx/rx
   // setting. Treat either spelling being true as enabled.
   const char *key = tx ? "fwdsp:recording.tx" : "fwdsp:recording.rx";
   if (cfg_get_bool(key, false)) {
      return cfg_get_bool(key, false);
   }
   return cfg_get_bool(tx ? "record.tx" : "record.rx", false);
}

// Recording direction describes the radio, not the encoder/decoder process.
static void media_record_channel(struct rr_mediachan *channel, rrconn_t *talker, bool start,
   const char *recording_id) {
   bool tx = channel->direction == RR_BINFRAME_DIR_TX;
   if (!channel->codec[0]) {
      if (start) {
         Log(LOG_DEBUG, "record", "No %s codec selected for channel %s; recording not armed",
            tx ? "TX" : "RX", channel->uuid);
      }
      return;
   }
   if (!fwdsp_find_channel_instance(channel->codec, !tx, channel->uuid)) {
      if (start) {
         Log(LOG_DEBUG, "record", "No fwdsp pipeline for %s channel %s; recording not armed",
            tx ? "TX" : "RX", channel->uuid);
      }
      return;
   }
   if (start && !media_recording_enabled(tx)) {
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
   const char *record_file = tx ? rr_ptt_recording_file((rr_vfo_t)channel->vfo) : NULL;
   bool failed = start ? fwdsp_cmd_start_record_named_file(channel->codec, !tx,
      channel->uuid, tx ? talker->chatname : "radio", tx, recording_id, record_file) :
      fwdsp_cmd_stop_record_channel(channel->codec, !tx, channel->uuid);
   if (failed) {
      Log(LOG_WARN, "record", "Unable to %s recording for channel %s",
         start ? "start" : "stop", channel->uuid);
   } else {
      if (media_record_log_transition(channel->uuid, start)) {
         Log(LOG_INFO, "record", "%s %s recording for channel %s (%s)%s",
            start ? "Armed" : "Stopped", (tx ? "TX" : "RX"), channel->uuid,
            channel->codec, (start ? "; file is created when samples arrive" : ""));
      }
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
      bool recording = media_recording_enabled(false);
      if (!recording || (!always && !clients)) {
         fwdsp_cmd_stop_record_channel(channel->codec, true, channel->uuid);
      } else {
         media_record_channel(channel, NULL, true, NULL);
      }
   }
}

void rrserver_media_record_ptt(rr_vfo_t vfo, bool ptt, rrconn_t *talker,
   const char *recording_id) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return;
   }
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0);
   if (channel) {
      media_record_channel(channel, talker, ptt, recording_id);
   }
}

// TX decoders are deliberately lazy. The channel codec can be negotiated
// while idle, but there is no reason to start a GStreamer process until a
// talker actually keys the VFO. This keeps idle rigs quiet and gives the
// codec selection/control round trip time to complete before samples arrive.
bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return false;
   }
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0);
   if (!channel || !channel->codec[0]) {
      Log(LOG_WARN, "ws.media", "PTT on VFO %s has no negotiated TX codec",
         vfo_name(vfo));
      return false;
   }

   if (!fwdsp_find_channel_instance(channel->codec, false, channel->uuid)) {
      int chan_id = fwdsp_codec_switch(NULL, channel->codec, false, channel->uuid);
      if (chan_id < 0) {
         Log(LOG_CRIT, "ws.media", "Failed to activate TX decoder %s.rx for %s",
            channel->codec, channel->uuid);
         return false;
      }
      Log(LOG_INFO, "ws.media", "Activated lazy TX decoder %s.rx (chan %d) for %s",
         channel->codec, chan_id, (talker ? talker->chatname : "unknown"));
   }
   if (!fwdsp_codec_set_pcm_callback(channel->codec, channel->uuid,
         rrserver_talker_pcm, NULL)) {
      Log(LOG_CRIT, "ws.media", "Unable to connect decoded TX PCM from %s to sink.rig0",
         channel->uuid);
      return false;
   }
   return true;
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
   rrconn_t *talker = whos_talking();
   bool tx_active = channel->direction == RR_BINFRAME_DIR_TX && talker &&
      talker->ptt_vfo == 'A' + channel->vfo;

   // TX codec changes while idle only update the channel's negotiated format.
   // The decoder is started lazily from rrserver_media_activate_ptt().
   if (channel->direction == RR_BINFRAME_DIR_TX && !tx_active) {
      if (old_codec && strlen(old_codec) == 4 &&
          strncmp(old_codec, codec, 4) != 0) {
         // If the previous PTT ended recently, release its warm decoder now
         // rather than leaving an obsolete codec process attached to the
         // channel while it is idle.
         fwdsp_codec_stop_channel(old_codec, false, channel->uuid);
      }
      Log(LOG_INFO, "ws.media", "Stored idle TX codec %s for %s; decoder deferred until PTT",
         codec, channel->uuid);
      dict_free(d);
      return;
   }
   // A codec switch changes the stream format and therefore starts a new
   // recording segment. Stop the old recorder explicitly before replacing the
   // fwdsp process; warm encoders otherwise keep a paused recorder alive
   // during fwdsp.hangtime. Keep the PTT ID for the replacement segment so
   // one PTT row still finds every file belonging to that transmission.
   bool codec_changed = old_codec && strlen(old_codec) == 4 &&
      strncmp(old_codec, codec, 4) != 0;
   if (codec_changed) {
      fwdsp_cmd_stop_record_channel(old_codec, fwdsp_tx, channel->uuid);
   }
   int chan_id = fwdsp_codec_switch(old_codec, codec, fwdsp_tx, channel->uuid);

   if (chan_id < 0) {
      Log(LOG_CRIT, "ws.media", "Failed to switch fwdsp pipeline to %s.%s for %s",
         codec, (fwdsp_tx ? "tx" : "rx"), channel->uuid);
      dict_free(d);
      return;
   }

   const char *recording_id = channel->direction == RR_BINFRAME_DIR_TX ?
      rr_ptt_recording_id((rr_vfo_t)channel->vfo) : NULL;
   if (channel->direction == RR_BINFRAME_DIR_RX ||
       (talker && talker->ptt_vfo == 'A' + channel->vfo)) {
      media_record_channel(channel, talker, true, recording_id);
   }

   Log(LOG_INFO, "ws.media", "Active fwdsp pipeline %s.%s (chan %d) for %s channel %s", codec,
      (fwdsp_tx ? "tx" : "rx"), chan_id, (cptr ? cptr->chatname : "?"), channel->uuid);
   dict_free(d);
}

// Late subscribers need container/codec headers before the next media packet.
static void rrserver_media_talker_frame(const char *event, const void *payload,
   size_t len, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;
   const uint8_t *data = payload;
   if (!cptr || !data || len == 0 || !cptr->is_ptt || cptr->ptt_vfo < 'A') return;
   rr_vfo_t vfo = (rr_vfo_t)(cptr->ptt_vfo - 'A');
   if (vfo < VFO_A || vfo >= MAX_VFOS) return;
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, 0);
   if (!channel || !channel->codec[0] || strlen(cptr->codec_tx) != 4 ||
       strncmp(channel->codec, cptr->codec_tx, 4) != 0 ||
       !fwdsp_write_channel_samples(channel->codec, false, channel->uuid, data, len)) {
      Log(LOG_WARN, "pcm.hub", "Unable to decode incoming TX audio for %s on VFO %c",
         cptr->chatname, cptr->ptt_vfo);
   }
}

static void rrserver_media_subscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   dict *d = data ? json2dict(data) : NULL;
   const char *uuid = d ? dict_get(d, "media.chan-uuid", NULL) : NULL;
   if (uuid && cptr) fwdsp_send_stream_headers(uuid, cptr);
   if (d) dict_free(d);
}

void rrserver_media_register_events(void) {
   event_on_binary("media.frame.tx", rrserver_media_talker_frame, NULL);
   event_on("media.subscribed", rrserver_media_subscribed, NULL);
   event_on("send-media-channels", rrserver_handle_send_media_channels, NULL);
   event_on("remove-media-channel", rrserver_handle_remove_media_channel, NULL);
   event_on("media.codec-select", rrserver_handle_codec_select, NULL);
}

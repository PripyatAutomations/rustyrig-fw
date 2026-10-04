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
#include <rrserver/rig.registry.h>
#include <rrserver/globalstate.h>
extern struct GlobalState rig;
#include <rrserver/rig.config.h>
#include <rrserver/ptt.h>

extern time_t now;

struct media_record_log_state {
   char uuid[64];
   bool active;
};
static struct media_record_log_state media_record_logs[MAX_MEDIA_CHANNELS];

static time_t rig_rx_write_warned[MAX_MEDIA_CHANNELS];
static time_t rig_tx_write_warned[MAX_MEDIA_CHANNELS];

static const char *rig_endpoint(rr_server_rig_t *radio, bool tx, char *buf, size_t len) {
   const char *alias = rr_rig_registry_alias(rig.rigs, radio);
   const char *configured = rr_rig_config_get(alias, tx ? "audio.sink" : "audio.source");
   if (configured && *configured) return configured;
   snprintf(buf, len, "%s.%s", tx ? "sink" : "src", alias);
   return buf;
}

static void rrserver_rig_rx_pcm(const char *name, const void *samples, size_t len,
   void *user_data) {
   (void)name;
   rr_server_rig_t *radio = user_data;
   uint8_t index = rr_rig_registry_media_index(rig.rigs, radio);
   for (int i = 0; i < MAX_MEDIA_CHANNELS; i++) {
      struct rr_mediachan *channel = &media_channels[i];
      if (!channel->uuid[0] || channel->subsystem != RR_BINFRAME_SUBSYS_AUDIO ||
          channel->direction != RR_BINFRAME_DIR_RX || channel->rig != index ||
          !channel->codec[0] || index == RR_BINFRAME_RIG_NA) {
         continue;
      }
      /* Tone and pink test codecs generate their own source in fwdsp. They
       * must not also be fed from the rig PCM hub. */
      if (channel->codec[3] == 'T' || channel->codec[3] == 'P') {
         continue;
      }
      char always_key[64];
      snprintf(always_key, sizeof(always_key), "record.always.vfo_%c", 'a' + channel->vfo);
      if (!ws_media_channel_has_subscribers(channel) && !cfg_get_bool(always_key, false)) {
         continue;
      }
      /* A channel can become active before its codec-select event has
       * finished (and a child can also have exited between two PCM ticks).
       * Make the encoder lookup self-healing here.  This is deliberately
       * limited to subscribed/always-recorded channels, so an idle VFO does
       * not start a fwdsp process merely because the PCM source is running.
       */
      if (!fwdsp_find_channel_instance(channel->codec, true, channel->uuid)) {
         if (fwdsp_codec_start(channel->codec, true, channel->uuid) < 0 &&
             (rig_rx_write_warned[i] == 0 || now < rig_rx_write_warned[i] ||
              now - rig_rx_write_warned[i] >= 5)) {
            rig_rx_write_warned[i] = now;
            Log(LOG_WARN, "pcm.hub", "Unable to start %s.tx for channel %s",
               channel->codec, channel->uuid);
         }
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
   struct rr_mediachan *channel = media_chan_find_uuid(channel_uuid);
   rr_server_rig_t *radio = channel ? rr_rig_registry_find_uuid(rig.rigs, channel->rig_uuid) : NULL;
   if (!radio) return;
   char endpoint[128];
   const char *sink = rig_endpoint(radio, true, endpoint, sizeof(endpoint));
   size_t slot = channel - media_channels;
   if (!fwdsp_processor_write(sink, samples, len) &&
       (rig_tx_write_warned[slot] == 0 || now < rig_tx_write_warned[slot] ||
        now - rig_tx_write_warned[slot] >= 5)) {
      rig_tx_write_warned[slot] = now;
      Log(LOG_WARN, "pcm.hub", "Unable to feed decoded talker PCM to %s (%s)", sink, channel_uuid);
   }
}

static bool start_rig_audio(rr_server_rig_t *radio, void *user) {
   (void)user;
   char rx_buf[128], tx_buf[128];
   const char *rx = rig_endpoint(radio, false, rx_buf, sizeof(rx_buf));
   const char *tx = rig_endpoint(radio, true, tx_buf, sizeof(tx_buf));
   bool source_ok = fwdsp_audio_capture_start(rx, NULL, rrserver_rig_rx_pcm, radio);
   bool sink_ok = fwdsp_audio_playback_start(tx, NULL);
   if (!source_ok || !sink_ok) {
      Log(LOG_WARN, "pcm.hub", "Rig %s PCM endpoints: %s=%s, %s=%s",
         rr_rig_registry_alias(rig.rigs, radio), rx, source_ok ? "ready" : "unavailable",
         tx, sink_ok ? "ready" : "unavailable");
   }
   return !source_ok || !sink_ok;
}

bool rrserver_media_audio_init(void) {
   return rig.rigs && !rr_rig_registry_foreach(rig.rigs, start_rig_audio, NULL);
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
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, rig.ptt_rig ? rr_rig_registry_media_index(rig.rigs, rig.ptt_rig) : 0);
   if (channel) {
      media_record_channel(channel, talker, ptt, recording_id);
   }
}

// TX decoders are normally prewarmed when the client subscribes so the first
// PTT frame does not pay GStreamer startup latency. Keep this fallback for a
// decoder that exited or a channel that was activated without a subscription.
bool rrserver_media_activate_ptt(rr_vfo_t vfo, rrconn_t *talker) {
   if (vfo < VFO_A || vfo >= MAX_VFOS) {
      return false;
   }
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_TX, (uint8_t)vfo, rig.ptt_rig ? rr_rig_registry_media_index(rig.rigs, rig.ptt_rig) : 0);
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
      Log(LOG_INFO, "ws.media", "Activated fallback TX decoder %s.rx (chan %d) for %s",
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

struct media_setup_context {
   rr_server_rig_t *radio;
   int made;
};

static bool media_setup_vfo(rr_server_vfo_t *vfo, void *user) {
   struct media_setup_context *ctx = user;
   rr_vfo_t index;
   if (!rr_server_vfo_native_index(vfo, &index) ||
       !rr_backend_vfo_supported(ctx->radio, vfo)) return false;
   const char *alias = rr_rig_registry_alias(rig.rigs, ctx->radio);
   const char *room = rr_rig_registry_room(rig.rigs, ctx->radio);
   if (!room) return true;
   uint8_t rig_index = rr_rig_registry_media_index(rig.rigs, ctx->radio);
   for (int tx = 0; tx < 2; tx++) {
      char descr[128];
      snprintf(descr, sizeof(descr), "%s audio %s VFO %s", tx ? "TX" : "RX",
         alias, rr_server_vfo_alias(vfo));
      struct rr_mediachan *cp = media_chan_add(RR_BINFRAME_SUBSYS_AUDIO,
         tx ? RR_BINFRAME_DIR_TX : RR_BINFRAME_DIR_RX, index, rig_index, NULL, descr);
      if (!cp) return true;
      snprintf(cp->room, sizeof(cp->room), "%s", room);
      snprintf(cp->rig_uuid, sizeof(cp->rig_uuid), "%s", rr_server_rig_id(ctx->radio));
      snprintf(cp->vfo_uuid, sizeof(cp->vfo_uuid), "%s", rr_server_vfo_id(vfo));
      snprintf(cp->name, sizeof(cp->name), "%s.vfo_%s.%s", alias,
         rr_server_vfo_alias(vfo), tx ? "tx" : "rx");
   }
   ctx->made++;
   return false;
}

static bool media_setup_rig(rr_server_rig_t *radio, void *user) {
   int *made = user;
   struct media_setup_context ctx = { .radio = radio };
   bool failed = rr_server_vfo_foreach(radio, media_setup_vfo, &ctx);
   *made += ctx.made;
   return failed;
}

bool rrserver_media_init(void) {
   int made = 0;
   if (rr_rig_registry_foreach(rig.rigs, media_setup_rig, &made)) {
      Log(LOG_CRIT, "ws.media", "Unable to provision all rig media channels");
      return true;
   }
   Log(LOG_INFO, "ws.media", "Provisioned media channels for %d rig VFO(s)", made);
   return false;
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
   if (channel->direction == RR_BINFRAME_DIR_TX &&
       !fwdsp_codec_set_pcm_callback(codec, channel->uuid,
          rrserver_talker_pcm, NULL)) {
      Log(LOG_CRIT, "ws.media", "Unable to connect prewarmed TX decoder for %s",
         channel->uuid);
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
   struct rr_binframe frame;
   if (!cptr || rr_binframe_parse(payload, len, &frame) < 0) return;
   struct rr_mediachan *channel = media_chan_find(frame.hdr.subsystem,
      frame.hdr.direction, frame.hdr.vfo, frame.hdr.rig);
   if (!channel || !channel->codec[0] || !media_client_in_channel_room(cptr, channel)) return;
   if (rig.ptt_rig && strcmp(channel->rig_uuid, rr_server_rig_id(rig.ptt_rig))) return;
   if (!fwdsp_write_channel_samples(channel->codec, false, channel->uuid, frame.data, frame.len))
      Log(LOG_WARN, "pcm.hub", "Unable to decode incoming TX audio for %s on channel %s",
         cptr->chatname, channel->uuid);
}

static void rrserver_media_subscribed(const char *event, const char *data,
   rrconn_t *cptr, void *user) {
   dict *d = data ? json2dict(data) : NULL;
   const char *uuid = d ? dict_get(d, "media.chan-uuid", NULL) : NULL;
   struct rr_mediachan *channel = uuid ? media_chan_find_uuid(uuid) : NULL;

   /* Prewarm both audio directions as soon as subscription and codec are
    * confirmed. The subscriber sweep keeps these processes referenced while
    * the client remains attached, so first audio and first PTT do not pay
    * GStreamer startup latency. */
   if (channel && channel->subsystem == RR_BINFRAME_SUBSYS_AUDIO &&
       channel->codec[0]) {
      bool fwdsp_tx = channel->direction == RR_BINFRAME_DIR_RX;
      struct fwdsp_subproc *sp = fwdsp_find_channel_instance(channel->codec,
         fwdsp_tx, channel->uuid);
      int chan_id = sp ? sp->chan_id :
         fwdsp_codec_start(channel->codec, fwdsp_tx, channel->uuid);

      if (chan_id < 0) {
         Log(LOG_CRIT, "ws.media", "Failed to prewarm subscribed %s %s.%s for %s",
            channel->direction == RR_BINFRAME_DIR_RX ? "RX encoder" : "TX decoder",
            channel->codec, fwdsp_tx ? "tx" : "rx", channel->uuid);
      } else {
         if (channel->direction == RR_BINFRAME_DIR_TX &&
             !fwdsp_codec_set_pcm_callback(channel->codec, channel->uuid,
                rrserver_talker_pcm, NULL)) {
            Log(LOG_CRIT, "ws.media", "Unable to connect prewarmed TX decoder for %s",
               channel->uuid);
         }
         if (channel->direction == RR_BINFRAME_DIR_RX) {
            media_record_channel(channel, NULL, true, NULL);
         }
         if (!sp) {
            Log(LOG_INFO, "ws.media", "Prewarmed subscribed %s %s.%s (chan %d) for %s",
               channel->direction == RR_BINFRAME_DIR_RX ? "RX encoder" : "TX decoder",
               channel->codec, fwdsp_tx ? "tx" : "rx", chan_id, channel->uuid);
         }
      }
   }
   if (channel && cptr) fwdsp_send_stream_headers(channel->uuid, cptr);
   if (d) dict_free(d);
}

void rrserver_media_register_events(void) {
   event_on_binary("media.frame.tx.channel", rrserver_media_talker_frame, NULL);
   event_on("media.subscribed", rrserver_media_subscribed, NULL);
   event_on("send-media-channels", rrserver_handle_send_media_channels, NULL);
   event_on("remove-media-channel", rrserver_handle_remove_media_channel, NULL);
   event_on("media.codec-select", rrserver_handle_codec_select, NULL);
}
